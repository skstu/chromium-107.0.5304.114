// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/web_cache/browser/web_cache_manager.h"

#include <string.h>

#include <algorithm>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/metrics/histogram_macros.h"
#include "base/no_destructor.h"
#include "base/system/sys_info.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/web_cache/public/features.h"

using base::Time;

namespace web_cache {

constexpr uint64_t kNoCapacitySet = std::numeric_limits<uint64_t>::max();

WebCacheManager::WebCacheInfo::WebCacheInfo() : last_capacity(kNoCapacitySet) {}
WebCacheManager::WebCacheInfo::~WebCacheInfo() = default;

static const int kReviseAllocationDelayMS = 200;

// The default size limit of the in-memory cache is 8 MB
static const int kDefaultMemoryCacheSize = 8 * 1024 * 1024;

namespace {

int GetDefaultCacheSize() {
  // Start off with a modest default
  int default_cache_size = kDefaultMemoryCacheSize;

  // Check how much physical memory the OS has
  int mem_size_mb = base::SysInfo::AmountOfPhysicalMemoryMB();
  if (mem_size_mb >= 1000)  // If we have a GB of memory, set a larger default.
    default_cache_size *= 4;
  else if (mem_size_mb >= 512)  // With 512 MB, set a slightly larger default.
    default_cache_size *= 2;

  return default_cache_size;
}

}  // anonymous namespace

// static
WebCacheManager* WebCacheManager::GetInstance() {
  static base::NoDestructor<WebCacheManager> s_instance;
  return s_instance.get();
}

WebCacheManager::WebCacheManager()
    : global_size_limit_(GetDefaultGlobalSizeLimit()) {
}

WebCacheManager::~WebCacheManager() {
}

void WebCacheManager::Add(int renderer_id) {
  DCHECK(inactive_renderers_.count(renderer_id) == 0);
  DCHECK(active_renderers_.count(renderer_id) == 0);
  active_renderers_.insert(renderer_id);

  RendererInfo* stats = &(stats_[renderer_id]);
  memset(stats, 0, sizeof(*stats));
  stats->access = Time::Now();

  content::RenderProcessHost* host =
      content::RenderProcessHost::FromID(renderer_id);
  if (host) {
    mojo::Remote<mojom::WebCache> service;
    host->BindReceiver(service.BindNewPipeAndPassReceiver());
    web_cache_services_[renderer_id].service = std::move(service);
  }

  // Revise our allocation strategy to account for this new renderer.
  ReviseAllocationStrategyLater();
}

void WebCacheManager::Remove(int renderer_id) {
  // Erase all knowledge of this renderer
  active_renderers_.erase(renderer_id);
  inactive_renderers_.erase(renderer_id);
  stats_.erase(renderer_id);

  web_cache_services_.erase(renderer_id);

  // Reallocate the resources used by this renderer
  ReviseAllocationStrategyLater();
}

void WebCacheManager::ObserveActivity(int renderer_id) {
  auto item = stats_.find(renderer_id);
  if (item == stats_.end())
    return;  // We might see stats for a renderer that has been destroyed.

  auto active_elmt = active_renderers_.find(renderer_id);
  auto inactive_elmt = inactive_renderers_.find(renderer_id);

  // Record activity, but only if we already received the notification that the
  // render process host exist. We might have stats from a destroyed renderer
  // that is about to be recreated for a new tab from which we're already
  // receiving activity.
  if (active_elmt != active_renderers_.end() ||
      inactive_elmt != inactive_renderers_.end()) {
    active_renderers_.insert(renderer_id);
    item->second.access = Time::Now();
  }

  if (inactive_elmt != inactive_renderers_.end()) {
    inactive_renderers_.erase(inactive_elmt);

    // A renderer that was inactive, just became active.  We should make sure
    // it is given a fair cache allocation, but we defer this for a bit in
    // order to make this function call cheap.
    ReviseAllocationStrategyLater();
  }
}

void WebCacheManager::ObserveStats(int renderer_id,
                                   uint64_t capacity,
                                   uint64_t size) {
  auto entry = stats_.find(renderer_id);
  if (entry == stats_.end())
    return;  // We might see stats for a renderer that has been destroyed.

  // Record the updated stats.
  entry->second.capacity = capacity;
  entry->second.size = size;
}

void WebCacheManager::SetGlobalSizeLimit(uint64_t bytes) {
  global_size_limit_ = bytes;
  ReviseAllocationStrategyLater();
}

void WebCacheManager::ClearCache() {
  // Tell each renderer process to clear the cache.
  ClearRendererCache(active_renderers_, INSTANTLY);
  ClearRendererCache(inactive_renderers_, INSTANTLY);
}

void WebCacheManager::ClearCacheOnNavigation() {
  // Tell each renderer process to clear the cache when a tab is reloaded or
  // the user navigates to a new website.
  ClearRendererCache(active_renderers_, ON_NAVIGATION);
  ClearRendererCache(inactive_renderers_, ON_NAVIGATION);
}

void WebCacheManager::OnRenderProcessHostCreated(
    content::RenderProcessHost* process_host) {
  Add(process_host->GetID());
  rph_observations_.AddObservation(process_host);
}

void WebCacheManager::RenderProcessExited(
    content::RenderProcessHost* process_host,
    const content::ChildProcessTerminationInfo& info) {
  RenderProcessHostDestroyed(process_host);
}

void WebCacheManager::RenderProcessHostDestroyed(
    content::RenderProcessHost* process_host) {
  rph_observations_.RemoveObservation(process_host);
  Remove(process_host->GetID());
}

// static
uint64_t WebCacheManager::GetDefaultGlobalSizeLimit() {
  return GetDefaultCacheSize();
}

void WebCacheManager::GatherStats(const std::set<int>& renderers,
                                  uint64_t* capacity,
                                  uint64_t* size) {
  *capacity = *size = 0;

  auto iter = renderers.begin();
  while (iter != renderers.end()) {
    auto elmt = stats_.find(*iter);
    if (elmt != stats_.end()) {
      *capacity += elmt->second.capacity;
      *size += elmt->second.size;
    }
    ++iter;
  }
}

// static
uint64_t WebCacheManager::GetSize(AllocationTactic tactic, uint64_t size) {
  switch (tactic) {
  case DIVIDE_EVENLY:
    // We aren't going to reserve any space for existing objects.
    return 0;
  case KEEP_CURRENT_WITH_HEADROOM:
    // We need enough space for our current objects, plus some headroom.
    return 3 * GetSize(KEEP_CURRENT, size) / 2;
  case KEEP_CURRENT:
    // We need enough space to keep our current objects.
    return size;
  default:
    NOTREACHED() << "Unknown cache allocation tactic";
    return 0;
  }
}

bool WebCacheManager::AttemptTactic(AllocationTactic active_tactic,
                                    uint64_t active_used_size,
                                    AllocationTactic inactive_tactic,
                                    uint64_t inactive_used_size,
                                    AllocationStrategy* strategy) {
  DCHECK(strategy);

  uint64_t active_size = GetSize(active_tactic, active_used_size);
  uint64_t inactive_size = GetSize(inactive_tactic, inactive_used_size);

  // Give up if we don't have enough space to use this tactic.
  if (global_size_limit_ < active_size + inactive_size)
    return false;

  // Compute the unreserved space available.
  uint64_t total_extra = global_size_limit_ - (active_size + inactive_size);

  // The plan for the extra space is to divide it evenly amoung the active
  // renderers.
  uint64_t shares = active_renderers_.size();

  // The inactive renderers get one share of the extra memory to be divided
  // among themselves.
  uint64_t inactive_extra = 0;
  if (!inactive_renderers_.empty()) {
    ++shares;
    inactive_extra = total_extra / shares;
  }

  // The remaining memory is allocated to the active renderers.
  uint64_t active_extra = total_extra - inactive_extra;

  // Actually compute the allocations for each renderer.
  AddToStrategy(active_renderers_, active_tactic, active_extra, strategy);
  AddToStrategy(inactive_renderers_, inactive_tactic, inactive_extra, strategy);

  // We succeeded in computing an allocation strategy.
  return true;
}

void WebCacheManager::AddToStrategy(const std::set<int>& renderers,
                                    AllocationTactic tactic,
                                    uint64_t extra_bytes_to_allocate,
                                    AllocationStrategy* strategy) {
  DCHECK(strategy);

  // Nothing to do if there are no renderers.  It is common for there to be no
  // inactive renderers if there is a single active tab.
  if (renderers.empty())
    return;

  // Divide the extra memory evenly among the renderers.
  uint64_t extra_each = extra_bytes_to_allocate / renderers.size();

  auto iter = renderers.begin();
  while (iter != renderers.end()) {
    uint64_t cache_size = extra_each;

    // Add in the space required to implement |tactic|.
    auto elmt = stats_.find(*iter);
    if (elmt != stats_.end()) {
      cache_size += GetSize(tactic, elmt->second.size);
    }

    // Record the allocation in our strategy.
    strategy->push_back(Allocation(*iter, cache_size));
    ++iter;
  }
}

void WebCacheManager::EnactStrategy(const AllocationStrategy& strategy) {
  for (auto& [render_process_id, new_capacity] : strategy) {
    content::RenderProcessHost* host =
        content::RenderProcessHost::FromID(render_process_id);
    if (!host)
      continue;

    // Find the mojo::Remote<WebCache> by renderer process id.
    auto it = web_cache_services_.find(render_process_id);
    if (it == web_cache_services_.end())
      continue;

    WebCacheInfo& cache_info = it->second;
    if (cache_info.last_capacity == new_capacity)
      continue;

    DCHECK(cache_info.service);
    cache_info.service->SetCacheCapacity(new_capacity);
    cache_info.last_capacity = new_capacity;
  }
}

void WebCacheManager::ClearCacheForProcess(int render_process_id) {
  std::set<int> renderers;
  renderers.insert(render_process_id);
  ClearRendererCache(renderers, INSTANTLY);
}

void WebCacheManager::ClearRendererCache(
    const std::set<int>& renderers,
    WebCacheManager::ClearCacheOccasion occasion) {
  auto iter = renderers.begin();
  for (; iter != renderers.end(); ++iter) {
    content::RenderProcessHost* host =
        content::RenderProcessHost::FromID(*iter);
    if (host) {
      // Find the mojo::Remote<WebCache> by renderer process id.
      auto it = web_cache_services_.find(*iter);
      if (it != web_cache_services_.end()) {
        WebCacheInfo& cache_info = it->second;
        DCHECK(cache_info.service);
        cache_info.service->ClearCache(occasion == ON_NAVIGATION);
      }
    }
  }
}

void WebCacheManager::ReviseAllocationStrategy() {
  DCHECK(!base::FeatureList::IsEnabled(kTrimWebCacheOnMemoryPressureOnly));

  DCHECK(stats_.size() <=
      active_renderers_.size() + inactive_renderers_.size());

  callback_pending_ = false;

  // Check if renderers have gone inactive.
  FindInactiveRenderers();

  // Gather statistics
  uint64_t active_capacity, active_size, inactive_capacity, inactive_size;
  GatherStats(active_renderers_, &active_capacity, &active_size);
  GatherStats(inactive_renderers_, &inactive_capacity, &inactive_size);

  // Compute an allocation strategy.
  //
  // We attempt various tactics in order of preference.  Our first preference
  // is not to evict any objects.  If we don't have enough resources, we'll
  // first try to evict dead data only.  If that fails, we'll just divide the
  // resources we have evenly.
  //
  // We always try to give the active renderers some head room in their
  // allocations so they can take memory away from an inactive renderer with
  // a large cache allocation.
  //
  // Notice the early exit will prevent attempting less desirable tactics once
  // we've found a workable strategy.
  AllocationStrategy strategy;
  if (  // Ideally, we'd like to give the active renderers some headroom and
      // keep all our current objects.
      AttemptTactic(KEEP_CURRENT_WITH_HEADROOM, active_size, KEEP_CURRENT,
                    inactive_size, &strategy) ||
      // Next, we try to keep the current objects in the active renders (with
      // some room for new objects) and give whatever is left to the inactive
      // renderers.
      AttemptTactic(KEEP_CURRENT_WITH_HEADROOM, active_size, DIVIDE_EVENLY,
                    inactive_size, &strategy) ||
      // If we've gotten this far, then we are very tight on memory.  Let's try
      // to at least keep around the live objects for the active renderers.
      AttemptTactic(KEEP_CURRENT, active_size, DIVIDE_EVENLY, inactive_size,
                    &strategy) ||
      // We're basically out of memory.  The best we can do is just divide up
      // what we have and soldier on.
      AttemptTactic(DIVIDE_EVENLY, active_size, DIVIDE_EVENLY, inactive_size,
                    &strategy)) {
    // Having found a workable strategy, we enact it.
    EnactStrategy(strategy);
  } else {
    // DIVIDE_EVENLY / DIVIDE_EVENLY should always succeed.
    NOTREACHED() << "Unable to find a cache allocation";
  }
}

void WebCacheManager::ReviseAllocationStrategyLater() {
  if (base::FeatureList::IsEnabled(kTrimWebCacheOnMemoryPressureOnly))
    return;

  // Avoid piling up notifications.
  if (callback_pending_)
    return;

  callback_pending_ = true;

  // Ask to be called back in a few milliseconds to actually recompute our
  // allocation.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&WebCacheManager::ReviseAllocationStrategy,
                     weak_factory_.GetWeakPtr()),
      base::Milliseconds(kReviseAllocationDelayMS));
}

void WebCacheManager::FindInactiveRenderers() {
  auto iter = active_renderers_.begin();
  while (iter != active_renderers_.end()) {
    auto elmt = stats_.find(*iter);
    DCHECK(elmt != stats_.end());
    base::TimeDelta idle = Time::Now() - elmt->second.access;
    if (idle >= base::Minutes(kRendererInactiveThresholdMinutes)) {
      // Moved to inactive status.  This invalidates our iterator.
      inactive_renderers_.insert(*iter);
      active_renderers_.erase(*iter);
      iter = active_renderers_.begin();
      continue;
    }
    ++iter;
  }
}

}  // namespace web_cache
