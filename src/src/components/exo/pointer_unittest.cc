// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/exo/pointer.h"

#include "ash/constants/app_types.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shell.h"
#include "ash/wm/desks/desks_util.h"
#include "ash/wm/window_positioning_utils.h"
#include "base/bind.h"
#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "build/chromeos_buildflags.h"
#include "chromeos/ui/base/window_properties.h"
#include "components/exo/buffer.h"
#include "components/exo/data_source.h"
#include "components/exo/data_source_delegate.h"
#include "components/exo/pointer_constraint_delegate.h"
#include "components/exo/pointer_delegate.h"
#include "components/exo/pointer_stylus_delegate.h"
#include "components/exo/relative_pointer_delegate.h"
#include "components/exo/seat.h"
#include "components/exo/shell_surface.h"
#include "components/exo/sub_surface.h"
#include "components/exo/surface.h"
#include "components/exo/test/exo_test_base.h"
#include "components/exo/test/exo_test_data_exchange_delegate.h"
#include "components/exo/test/exo_test_helper.h"
#include "components/exo/test/mock_security_delegate.h"
#include "components/exo/test/shell_surface_builder.h"
#include "components/viz/common/quads/compositor_frame.h"
#include "components/viz/service/surfaces/surface.h"
#include "components/viz/service/surfaces/surface_manager.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/client/focus_client.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom-shared.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/test/event_generator.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/vector2d_f.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#include "ash/drag_drop/drag_drop_controller.h"
#include "base/test/bind.h"
#include "components/exo/wm_helper.h"
#include "ui/aura/client/drag_drop_client.h"
#include "ui/aura/window_tree_host.h"
#include "ui/events/test/events_test_utils.h"
#endif

using ::testing::_;
using ::testing::AnyNumber;

namespace exo {
namespace {

void DispatchGesture(ui::EventType gesture_type, gfx::Point location) {
  ui::GestureEventDetails event_details(gesture_type);
  ui::GestureEvent gesture_event(location.x(), location.y(), 0,
                                 ui::EventTimeForNow(), event_details);
  ui::EventSource* event_source =
      ash::Shell::GetPrimaryRootWindow()->GetHost()->GetEventSource();
  ui::EventSourceTestApi event_source_test(event_source);
  ui::EventDispatchDetails details =
      event_source_test.SendEventToSink(&gesture_event);
  CHECK(!details.dispatcher_destroyed);
}

class MockPointerDelegate : public PointerDelegate {
 public:
  MockPointerDelegate() {}

  // Overridden from PointerDelegate:
  MOCK_METHOD1(OnPointerDestroying, void(Pointer*));
  MOCK_CONST_METHOD1(CanAcceptPointerEventsForSurface, bool(Surface*));
  MOCK_METHOD3(OnPointerEnter, void(Surface*, const gfx::PointF&, int));
  MOCK_METHOD1(OnPointerLeave, void(Surface*));
  MOCK_METHOD2(OnPointerMotion, void(base::TimeTicks, const gfx::PointF&));
  MOCK_METHOD3(OnPointerButton, void(base::TimeTicks, int, bool));
  MOCK_METHOD3(OnPointerScroll,
               void(base::TimeTicks, const gfx::Vector2dF&, bool));
  MOCK_METHOD1(OnPointerScrollStop, void(base::TimeTicks));
  MOCK_METHOD0(OnPointerFrame, void());
};

class MockRelativePointerDelegate : public RelativePointerDelegate {
 public:
  MockRelativePointerDelegate() = default;
  ~MockRelativePointerDelegate() override = default;

  // Overridden from RelativePointerDelegate:
  MOCK_METHOD1(OnPointerDestroying, void(Pointer*));
  MOCK_METHOD3(OnPointerRelativeMotion,
               void(base::TimeTicks,
                    const gfx::Vector2dF&,
                    const gfx::Vector2dF&));
};

class MockPointerConstraintDelegate : public PointerConstraintDelegate {
 public:
  MockPointerConstraintDelegate() {
    ON_CALL(*this, OnConstraintActivated).WillByDefault([this]() {
      activated_count++;
    });
    ON_CALL(*this, OnConstraintBroken).WillByDefault([this]() {
      broken_count++;
    });
  }
  ~MockPointerConstraintDelegate() override = default;

  // Overridden from PointerConstraintDelegate:
  MOCK_METHOD0(OnConstraintActivated, void());
  MOCK_METHOD0(OnAlreadyConstrained, void());
  MOCK_METHOD0(OnConstraintBroken, void());
  MOCK_METHOD0(IsPersistent, bool());
  MOCK_METHOD0(GetConstrainedSurface, Surface*());
  MOCK_METHOD0(OnDefunct, void());

  int activated_count = 0;
  int broken_count = 0;
};

class MockPointerStylusDelegate : public PointerStylusDelegate {
 public:
  MockPointerStylusDelegate() {}

  // Overridden from PointerStylusDelegate:
  MOCK_METHOD(void, OnPointerDestroying, (Pointer*));
  MOCK_METHOD(void, OnPointerToolChange, (ui::EventPointerType));
  MOCK_METHOD(void, OnPointerForce, (base::TimeTicks, float));
  MOCK_METHOD(void, OnPointerTilt, (base::TimeTicks, const gfx::Vector2dF&));
};

class TestDataSourceDelegate : public DataSourceDelegate {
 public:
  TestDataSourceDelegate() {}

  TestDataSourceDelegate(const TestDataSourceDelegate&) = delete;
  TestDataSourceDelegate& operator=(const TestDataSourceDelegate&) = delete;

  // Overridden from DataSourceDelegate:
  void OnDataSourceDestroying(DataSource* device) override {}
  void OnTarget(const absl::optional<std::string>& mime_type) override {}
  void OnSend(const std::string& mime_type, base::ScopedFD fd) override {}
  void OnCancelled() override {}
  void OnDndDropPerformed() override {}
  void OnDndFinished() override {}
  void OnAction(DndAction dnd_action) override {}
  bool CanAcceptDataEventsForSurface(Surface* surface) const override {
    return true;
  }
};

class PointerTest : public test::ExoTestBase {
 public:
  PointerTest() = default;

  PointerTest(const PointerTest&) = delete;
  PointerTest& operator=(const PointerTest&) = delete;

  void SetUp() override {
    test::ExoTestBase::SetUp();
    // Sometimes underlying infra (i.e. X11 / Xvfb) may emit pointer events
    // which can break MockPointerDelegate's expectations, so they should be
    // consumed before starting. See https://crbug.com/854674.
    base::RunLoop().RunUntilIdle();
  }
};

#if BUILDFLAG(IS_CHROMEOS_ASH)
class PointerConstraintTest : public PointerTest {
 public:
  PointerConstraintTest() = default;
  PointerConstraintTest(const PointerConstraintTest&) = delete;
  PointerConstraintTest& operator=(const PointerConstraintTest&) = delete;

  void SetUp() override {
    PointerTest::SetUp();

    shell_surface_ = BuildShellSurfaceWhichPermitsPointerLock();
    surface_ = shell_surface_->surface_for_testing();
    seat_ = std::make_unique<Seat>();
    pointer_ = std::make_unique<Pointer>(&delegate_, seat_.get());

    focus_client_ =
        aura::client::GetFocusClient(ash::Shell::GetPrimaryRootWindow());
    focus_client_->FocusWindow(surface_->window());

    generator_ = std::make_unique<ui::test::EventGenerator>(
        ash::Shell::GetPrimaryRootWindow());

    EXPECT_CALL(delegate_, CanAcceptPointerEventsForSurface(surface_))
        .WillRepeatedly(testing::Return(true));

    EXPECT_CALL(constraint_delegate_, GetConstrainedSurface())
        .WillRepeatedly(testing::Return(surface_));
  }

  void TearDown() override {
    // Many objects need to be destroyed before teardown for various reasons.
    seat_.reset();
    shell_surface_.reset();
    surface_ = nullptr;

    // Some tests generate mouse events which Pointer::OnMouseEvent() handles
    // during the run loop. That routine accesses WMHelper. So, make sure any
    // such pending tasks finish before TearDown() destroys the WMHelper.
    base::RunLoop().RunUntilIdle();

    PointerTest::TearDown();
  }

  std::unique_ptr<ShellSurface> BuildShellSurfaceWhichPermitsPointerLock() {
    std::unique_ptr<ShellSurface> shell_surface =
        test::ShellSurfaceBuilder({10, 10}).BuildShellSurface();
    shell_surface->GetWidget()->GetNativeWindow()->SetProperty(
        chromeos::kUseOverviewToExitPointerLock, true);
    return shell_surface;
  }

  std::unique_ptr<ui::test::EventGenerator> generator_;
  std::unique_ptr<Pointer> pointer_;
  std::unique_ptr<Seat> seat_;
  testing::NiceMock<MockPointerConstraintDelegate> constraint_delegate_;
  testing::NiceMock<MockPointerDelegate> delegate_;
  std::unique_ptr<ShellSurface> shell_surface_;
  Surface* surface_;
  aura::client::FocusClient* focus_client_;
};
#endif

TEST_F(PointerTest, SetCursor) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(1);
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  std::unique_ptr<Surface> pointer_surface(new Surface);
  std::unique_ptr<Buffer> pointer_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  pointer_surface->Attach(pointer_buffer.get());
  pointer_surface->Commit();

  // Set pointer surface.
  pointer->SetCursor(pointer_surface.get(), gfx::Point(5, 5));
  base::RunLoop().RunUntilIdle();

  const viz::CompositorRenderPass* last_render_pass;
  {
    viz::SurfaceId surface_id = pointer->host_window()->GetSurfaceId();
    viz::SurfaceManager* surface_manager = GetSurfaceManager();
    ASSERT_TRUE(surface_manager->GetSurfaceForId(surface_id)->HasActiveFrame());
    const viz::CompositorFrame& frame =
        surface_manager->GetSurfaceForId(surface_id)->GetActiveFrame();
    EXPECT_EQ(gfx::Rect(0, 0, 10, 10),
              frame.render_pass_list.back()->output_rect);
    last_render_pass = frame.render_pass_list.back().get();
  }

  // Adjust hotspot.
  pointer->SetCursor(pointer_surface.get(), gfx::Point());
  base::RunLoop().RunUntilIdle();

  // Verify that adjustment to hotspot resulted in new frame.
  {
    viz::SurfaceId surface_id = pointer->host_window()->GetSurfaceId();
    viz::SurfaceManager* surface_manager = GetSurfaceManager();
    ASSERT_TRUE(surface_manager->GetSurfaceForId(surface_id)->HasActiveFrame());
    const viz::CompositorFrame& frame =
        surface_manager->GetSurfaceForId(surface_id)->GetActiveFrame();
    EXPECT_TRUE(frame.render_pass_list.back().get() != last_render_pass);
  }

  // Unset pointer surface.
  pointer->SetCursor(nullptr, gfx::Point());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, SetCursorNull) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(1);
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  pointer->SetCursor(nullptr, gfx::Point());
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(nullptr, pointer->root_surface());
  aura::client::CursorClient* cursor_client = aura::client::GetCursorClient(
      shell_surface->GetWidget()->GetNativeWindow()->GetRootWindow());
  EXPECT_EQ(ui::mojom::CursorType::kNone, cursor_client->GetCursor().type());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, SetCursorType) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(1);
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  pointer->SetCursorType(ui::mojom::CursorType::kIBeam);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(nullptr, pointer->root_surface());
  aura::client::CursorClient* cursor_client = aura::client::GetCursorClient(
      shell_surface->GetWidget()->GetNativeWindow()->GetRootWindow());
  EXPECT_EQ(ui::mojom::CursorType::kIBeam, cursor_client->GetCursor().type());

  // Set the pointer with surface after setting pointer type.
  std::unique_ptr<Surface> pointer_surface(new Surface);
  std::unique_ptr<Buffer> pointer_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  pointer_surface->Attach(pointer_buffer.get());
  pointer_surface->Commit();

  pointer->SetCursor(pointer_surface.get(), gfx::Point());
  base::RunLoop().RunUntilIdle();

  {
    viz::SurfaceId surface_id = pointer->host_window()->GetSurfaceId();
    viz::SurfaceManager* surface_manager = GetSurfaceManager();
    ASSERT_TRUE(surface_manager->GetSurfaceForId(surface_id)->HasActiveFrame());
    const viz::CompositorFrame& frame =
        surface_manager->GetSurfaceForId(surface_id)->GetActiveFrame();
    EXPECT_EQ(gfx::Rect(0, 0, 10, 10),
              frame.render_pass_list.back()->output_rect);
  }

  // Set the pointer type after the pointer surface is specified.
  pointer->SetCursorType(ui::mojom::CursorType::kCross);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(nullptr, pointer->root_surface());
  EXPECT_EQ(ui::mojom::CursorType::kCross, cursor_client->GetCursor().type());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, SetCursorTypeOutsideOfSurface) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin() -
                        gfx::Vector2d(1, 1));

  pointer->SetCursorType(ui::mojom::CursorType::kIBeam);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(nullptr, pointer->root_surface());
  aura::client::CursorClient* cursor_client = aura::client::GetCursorClient(
      shell_surface->GetWidget()->GetNativeWindow()->GetRootWindow());
  // The cursor type shouldn't be the specified one, since the pointer is
  // located outside of the surface.
  EXPECT_NE(ui::mojom::CursorType::kIBeam, cursor_client->GetCursor().type());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, SetCursorAndSetCursorType) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(1);
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  std::unique_ptr<Surface> pointer_surface(new Surface);
  std::unique_ptr<Buffer> pointer_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  pointer_surface->Attach(pointer_buffer.get());
  pointer_surface->Commit();

  // Set pointer surface.
  pointer->SetCursor(pointer_surface.get(), gfx::Point());
  EXPECT_EQ(1u, pointer->GetActivePresentationCallbacksForTesting().size());
  base::RunLoop().RunUntilIdle();

  {
    viz::SurfaceId surface_id = pointer->host_window()->GetSurfaceId();
    viz::SurfaceManager* surface_manager = GetSurfaceManager();
    ASSERT_TRUE(surface_manager->GetSurfaceForId(surface_id)->HasActiveFrame());
    const viz::CompositorFrame& frame =
        surface_manager->GetSurfaceForId(surface_id)->GetActiveFrame();
    EXPECT_EQ(gfx::Rect(0, 0, 10, 10),
              frame.render_pass_list.back()->output_rect);
  }

  // Set the cursor type to the kNone through SetCursorType.
  pointer->SetCursorType(ui::mojom::CursorType::kNone);
  EXPECT_TRUE(pointer->GetActivePresentationCallbacksForTesting().empty());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(nullptr, pointer->root_surface());

  // Set the same pointer surface again.
  pointer->SetCursor(pointer_surface.get(), gfx::Point());
  EXPECT_EQ(1u, pointer->GetActivePresentationCallbacksForTesting().size());
  auto& list =
      pointer->GetActivePresentationCallbacksForTesting().begin()->second;
  base::RunLoop runloop;
  list.push_back(base::BindRepeating(
      [](base::RepeatingClosure callback, const gfx::PresentationFeedback&) {
        callback.Run();
      },
      runloop.QuitClosure()));
  runloop.Run();

  {
    viz::SurfaceId surface_id = pointer->host_window()->GetSurfaceId();
    viz::SurfaceManager* surface_manager = GetSurfaceManager();
    ASSERT_TRUE(surface_manager->GetSurfaceForId(surface_id)->HasActiveFrame());
    const viz::CompositorFrame& frame =
        surface_manager->GetSurfaceForId(surface_id)->GetActiveFrame();
    EXPECT_EQ(gfx::Rect(0, 0, 10, 10),
              frame.render_pass_list.back()->output_rect);
  }

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, SetCursorNullAndSetCursorType) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(1);
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  // Set nullptr surface.
  pointer->SetCursor(nullptr, gfx::Point());
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(nullptr, pointer->root_surface());
  aura::client::CursorClient* cursor_client = aura::client::GetCursorClient(
      shell_surface->GetWidget()->GetNativeWindow()->GetRootWindow());
  EXPECT_EQ(ui::mojom::CursorType::kNone, cursor_client->GetCursor().type());

  // Set the cursor type.
  pointer->SetCursorType(ui::mojom::CursorType::kIBeam);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(nullptr, pointer->root_surface());
  EXPECT_EQ(ui::mojom::CursorType::kIBeam, cursor_client->GetCursor().type());

  // Set nullptr surface again.
  pointer->SetCursor(nullptr, gfx::Point());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(nullptr, pointer->root_surface());
  EXPECT_EQ(ui::mojom::CursorType::kNone, cursor_client->GetCursor().type());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerEnter) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(1);
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerLeave) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(4);
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerLeave(surface.get()));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().bottom_right());

  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerLeave(surface.get()));
  shell_surface.reset();
  surface.reset();

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerMotion) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(6);

  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(1, 1)));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin() +
                        gfx::Vector2d(1, 1));

  std::unique_ptr<Surface> sub_surface(new Surface);
  std::unique_ptr<SubSurface> sub(
      new SubSurface(sub_surface.get(), surface.get()));
  surface->SetSubSurfacePosition(sub_surface.get(), gfx::PointF(5, 5));
  gfx::Size sub_buffer_size(5, 5);
  std::unique_ptr<Buffer> sub_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(sub_buffer_size)));
  sub_surface->Attach(sub_buffer.get());
  sub_surface->Commit();
  surface->Commit();

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(sub_surface.get()))
      .WillRepeatedly(testing::Return(true));

  EXPECT_CALL(delegate, OnPointerLeave(surface.get()));
  EXPECT_CALL(delegate, OnPointerEnter(sub_surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(sub_surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(1, 1)));
  generator.MoveMouseTo(sub_surface->window()->GetBoundsInScreen().origin() +
                        gfx::Vector2d(1, 1));

  std::unique_ptr<Surface> child_surface(new Surface);
  std::unique_ptr<ShellSurface> child_shell_surface(new ShellSurface(
      child_surface.get(), gfx::Point(9, 9), /*can_minimize=*/false,
      ash::desks_util::GetActiveDeskContainerId()));
  child_shell_surface->DisableMovement();
  child_shell_surface->SetParent(shell_surface.get());
  gfx::Size child_buffer_size(15, 15);
  std::unique_ptr<Buffer> child_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(child_buffer_size)));
  child_surface->Attach(child_buffer.get());
  child_surface->Commit();

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(child_surface.get()))
      .WillRepeatedly(testing::Return(true));

  EXPECT_CALL(delegate, OnPointerLeave(sub_surface.get()));
  EXPECT_CALL(delegate, OnPointerEnter(child_surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(child_surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(10, 10)));
  generator.MoveMouseTo(child_surface->window()->GetBoundsInScreen().origin() +
                        gfx::Vector2d(10, 10));

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerButton) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(3);

  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate,
              OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, true));
  EXPECT_CALL(delegate,
              OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, false));
  generator.ClickLeftButton();

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerScroll) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());
  gfx::Point location = surface->window()->GetBoundsInScreen().origin();

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(3);

  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(location);

  {
    // Expect fling stop followed by scroll and scroll stop.
    testing::InSequence sequence;

    EXPECT_CALL(delegate,
                OnPointerScroll(testing::_, gfx::Vector2dF(1.2, 1.2), false));
    EXPECT_CALL(delegate, OnPointerScrollStop(testing::_));
  }
  generator.ScrollSequence(location, base::TimeDelta(), 1, 1, 1, 1);

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerScrollWithThreeFinger) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());
  gfx::Point location = surface->window()->GetBoundsInScreen().origin();

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(2);

  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(location);

  {
    // Expect no scroll.
    testing::InSequence sequence;
    EXPECT_CALL(delegate, OnPointerScrollStop(testing::_));
  }

  // Three fingers scroll.
  generator.ScrollSequence(location, base::TimeDelta(), 1, 1, 1,
                           3 /* num_fingers */);

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerScrollDiscrete) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(2);

  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate,
              OnPointerScroll(testing::_, gfx::Vector2dF(1, 1), true));
  generator.MoveMouseWheel(1, 1);

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, RegisterPointerEventsOnModal) {
  // Create modal surface.
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(
      new ShellSurface(surface.get(), gfx::Point(), /*can_minimize=*/false,
                       ash::kShellWindowId_SystemModalContainer));
  shell_surface->DisableMovement();
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(5, 5))));
  surface->Attach(buffer.get());
  surface->Commit();
  ash::CenterWindow(shell_surface->GetWidget()->GetNativeWindow());
  // Make the window modal.
  shell_surface->SetSystemModal(true);
  EXPECT_TRUE(ash::Shell::IsSystemModalWindowOpen());

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, OnPointerFrame()).Times(testing::AnyNumber());
  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));

  // Pointer events on modal window should be registered.
  gfx::Point origin = surface->window()->GetBoundsInScreen().origin();
  {
    testing::InSequence sequence;
    EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
    generator.MoveMouseTo(origin);

    EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(1, 1)));
    generator.MoveMouseTo(origin + gfx::Vector2d(1, 1));

    EXPECT_CALL(delegate,
                OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, true));
    EXPECT_CALL(delegate,
                OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, false));
    generator.ClickLeftButton();

    EXPECT_CALL(delegate,
                OnPointerScroll(testing::_, gfx::Vector2dF(1.2, 1.2), false));
    EXPECT_CALL(delegate, OnPointerScrollStop(testing::_));
    generator.ScrollSequence(origin, base::TimeDelta(), 1, 1, 1, 1);
  }

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, IgnorePointerEventsOnNonModalWhenModalIsOpen) {
  // Create surface for non-modal window.
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(10, 10))));
  surface->Attach(buffer.get());
  surface->Commit();

  // Create surface for modal window.
  std::unique_ptr<Surface> surface2(new Surface);
  std::unique_ptr<ShellSurface> shell_surface2(
      new ShellSurface(surface2.get(), gfx::Point(), /*can_minimize=*/false,
                       ash::kShellWindowId_SystemModalContainer));
  shell_surface2->DisableMovement();
  std::unique_ptr<Buffer> buffer2(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(5, 5))));
  surface2->Attach(buffer2.get());
  surface2->Commit();
  ash::CenterWindow(shell_surface2->GetWidget()->GetNativeWindow());
  // Make the window modal.
  shell_surface2->SetSystemModal(true);
  EXPECT_TRUE(ash::Shell::IsSystemModalWindowOpen());

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, OnPointerFrame()).Times(testing::AnyNumber());
  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface2.get()))
      .WillRepeatedly(testing::Return(true));

  // Check if pointer events on non-modal window are ignored.
  gfx::Point nonModalOrigin = surface->window()->GetBoundsInScreen().origin();
  {
    testing::InSequence sequence;
    EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0))
        .Times(0);
    generator.MoveMouseTo(nonModalOrigin);

    EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(1, 1)))
        .Times(0);
    generator.MoveMouseTo(nonModalOrigin + gfx::Vector2d(1, 1));

    EXPECT_CALL(delegate,
                OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, true))
        .Times(0);
    EXPECT_CALL(delegate,
                OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, false))
        .Times(0);
    generator.ClickLeftButton();

    EXPECT_CALL(delegate,
                OnPointerScroll(testing::_, gfx::Vector2dF(1.2, 1.2), false))
        .Times(0);
    EXPECT_CALL(delegate, OnPointerScrollStop(testing::_)).Times(0);
    generator.ScrollSequence(nonModalOrigin, base::TimeDelta(), 1, 1, 1, 1);

    EXPECT_CALL(delegate, OnPointerLeave(surface.get())).Times(0);
    generator.MoveMouseTo(
        surface->window()->GetBoundsInScreen().bottom_right());
  }

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, IgnorePointerLeaveOnModal) {
  // Create modal surface.
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(
      new ShellSurface(surface.get(), gfx::Point(), /*can_minimize=*/false,
                       ash::kShellWindowId_SystemModalContainer));
  shell_surface->DisableMovement();
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(5, 5))));
  surface->Attach(buffer.get());
  surface->Commit();
  ash::CenterWindow(shell_surface->GetWidget()->GetNativeWindow());
  // Make the window modal.
  shell_surface->SetSystemModal(true);
  EXPECT_TRUE(ash::Shell::IsSystemModalWindowOpen());

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, OnPointerFrame()).Times(testing::AnyNumber());
  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));

  gfx::Point origin = surface->window()->GetBoundsInScreen().origin();

  {
    testing::InSequence sequence;
    EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
    generator.MoveMouseTo(origin);

    // OnPointerLeave should not be called on the modal surface when the pointer
    // moves out of its bounds.
    EXPECT_CALL(delegate, OnPointerLeave(surface.get())).Times(0);
    generator.MoveMouseTo(
        surface->window()->GetBoundsInScreen().bottom_right());
  }

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, RegisterPointerEventsOnNonModal) {
  // Create surface for non-modal window.
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(10, 10))));
  surface->Attach(buffer.get());
  surface->Commit();

  // Create another surface for a non-modal window.
  std::unique_ptr<Surface> surface2(new Surface);
  std::unique_ptr<ShellSurface> shell_surface2(
      new ShellSurface(surface2.get(), gfx::Point(), /*can_minimize=*/false,
                       ash::kShellWindowId_SystemModalContainer));
  shell_surface2->DisableMovement();
  std::unique_ptr<Buffer> buffer2(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(5, 5))));
  surface2->Attach(buffer2.get());
  surface2->Commit();
  ash::CenterWindow(shell_surface2->GetWidget()->GetNativeWindow());

  MockPointerDelegate delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, OnPointerFrame()).Times(testing::AnyNumber());
  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface2.get()))
      .WillRepeatedly(testing::Return(true));

  // Ensure second window is non-modal.
  shell_surface2->SetSystemModal(false);
  EXPECT_FALSE(ash::Shell::IsSystemModalWindowOpen());

  // Check if pointer events on first non-modal window are registered.
  gfx::Point firstWindowOrigin =
      surface->window()->GetBoundsInScreen().origin();
  {
    testing::InSequence sequence;
    EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
    generator.MoveMouseTo(firstWindowOrigin);

    EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(1, 1)));
    generator.MoveMouseTo(firstWindowOrigin + gfx::Vector2d(1, 1));

    EXPECT_CALL(delegate,
                OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, true));
    EXPECT_CALL(delegate,
                OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, false));
    generator.ClickLeftButton();

    EXPECT_CALL(delegate,
                OnPointerScroll(testing::_, gfx::Vector2dF(1.2, 1.2), false));
    EXPECT_CALL(delegate, OnPointerScrollStop(testing::_));
    generator.ScrollSequence(firstWindowOrigin, base::TimeDelta(), 1, 1, 1, 1);

    EXPECT_CALL(delegate, OnPointerLeave(surface.get()));
    generator.MoveMouseTo(
        surface->window()->GetBoundsInScreen().bottom_right());
  }

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, DragDropAbort) {
  Seat seat(std::make_unique<TestDataExchangeDelegate>());
  MockPointerDelegate pointer_delegate;
  std::unique_ptr<Pointer> pointer(new Pointer(&pointer_delegate, &seat));
  TestDataSourceDelegate data_source_delegate;
  DataSource source(&data_source_delegate);
  Surface origin, icon;

  // Make origin into a real window so the pointer can click it
  ShellSurface shell_surface(&origin);
  Buffer buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(10, 10)));
  origin.Attach(&buffer);
  origin.Commit();

  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(pointer_delegate, CanAcceptPointerEventsForSurface(&origin))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(pointer_delegate, OnPointerFrame()).Times(3);
  EXPECT_CALL(pointer_delegate, OnPointerEnter(&origin, gfx::PointF(), 0));
  generator.MoveMouseTo(origin.window()->GetBoundsInScreen().origin());

  seat.StartDrag(&source, &origin, &icon, ui::mojom::DragEventSource::kMouse);
  EXPECT_TRUE(seat.get_drag_drop_operation_for_testing());

  EXPECT_CALL(pointer_delegate, OnPointerButton).Times(2);
  generator.PressLeftButton();
  EXPECT_TRUE(seat.get_drag_drop_operation_for_testing());
  generator.ReleaseLeftButton();
  EXPECT_FALSE(seat.get_drag_drop_operation_for_testing());

  EXPECT_CALL(pointer_delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerTest, DragDropAndPointerEnterLeaveEvents) {
  Seat seat(std::make_unique<TestDataExchangeDelegate>());
  MockPointerDelegate pointer_delegate;
  std::unique_ptr<Pointer> pointer(new Pointer(&pointer_delegate, &seat));
  TestDataSourceDelegate data_source_delegate;
  DataSource source(&data_source_delegate);
  Surface origin;

  // Make origin into a real window so the pointer can click it
  ShellSurface shell_surface(&origin);
  Buffer buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(10, 10)));
  origin.Attach(&buffer);
  origin.Commit();

  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(pointer_delegate, CanAcceptPointerEventsForSurface(&origin))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(pointer_delegate, OnPointerFrame()).Times(AnyNumber());
  EXPECT_CALL(pointer_delegate, OnPointerEnter(&origin, gfx::PointF(), 0));
  generator.MoveMouseTo(origin.window()->GetBoundsInScreen().origin());

  auto* drag_drop_controller = static_cast<ash::DragDropController*>(
      aura::client::GetDragDropClient(ash::Shell::GetPrimaryRootWindow()));
  ASSERT_TRUE(drag_drop_controller);

  generator.PressLeftButton();
  seat.StartDrag(&source, &origin, /*icon=*/nullptr,
                 ui::mojom::DragEventSource::kMouse);
  EXPECT_TRUE(seat.get_drag_drop_operation_for_testing());

  // As soon as the runloop gets triggered, emit a mouse release event.
  drag_drop_controller->SetLoopClosureForTesting(
      base::BindLambdaForTesting([&]() {
        EXPECT_CALL(pointer_delegate, OnPointerEnter(_, _, _));
        generator.ReleaseLeftButton();
      }),
      base::DoNothing());

  EXPECT_CALL(pointer_delegate, OnPointerLeave(_));
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(pointer_delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, DragDropAndPointerEnterLeaveEvents_NoOpOnTouchDrag) {
  Seat seat(std::make_unique<TestDataExchangeDelegate>());
  MockPointerDelegate pointer_delegate;
  std::unique_ptr<Pointer> pointer(new Pointer(&pointer_delegate, &seat));
  TestDataSourceDelegate data_source_delegate;
  DataSource source(&data_source_delegate);
  Surface origin;

  // Make origin into a real window so the pointer can click it
  ShellSurface shell_surface(&origin);
  Buffer buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(10, 10)));
  origin.Attach(&buffer);
  origin.Commit();

  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(pointer_delegate, CanAcceptPointerEventsForSurface(&origin))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(pointer_delegate, OnPointerFrame()).Times(AnyNumber());
  EXPECT_CALL(pointer_delegate, OnPointerEnter(&origin, gfx::PointF(), 0));
  generator.MoveMouseTo(origin.window()->GetBoundsInScreen().origin());

  auto* drag_drop_controller = static_cast<ash::DragDropController*>(
      aura::client::GetDragDropClient(ash::Shell::GetPrimaryRootWindow()));
  ASSERT_TRUE(drag_drop_controller);

  seat.StartDrag(&source, &origin, /*icon=*/nullptr,
                 ui::mojom::DragEventSource::kTouch);
  EXPECT_TRUE(seat.get_drag_drop_operation_for_testing());

  // Initiate the gesture sequence.
  DispatchGesture(ui::ET_GESTURE_BEGIN, gfx::Point(10, 10));

  // As soon as the runloop gets triggered, emit a mouse release event.
  drag_drop_controller->SetLoopClosureForTesting(
      base::BindLambdaForTesting([&]() {
        EXPECT_CALL(pointer_delegate, OnPointerEnter(_, _, _)).Times(0);
        // generator.ReleaseLeftButton();
        generator.set_current_screen_location(gfx::Point(10, 10));
        generator.PressMoveAndReleaseTouchBy(50, 50);
      }),
      base::DoNothing());

  EXPECT_CALL(pointer_delegate, OnPointerLeave(_)).Times(0);
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(pointer_delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, IgnoresHandledEvents) {
  // A very dumb handler that simply marks all events as handled. This is needed
  // allows us to mark a mouse event as handled as it gets processed by the
  // event processor.
  class SetHandledHandler : public ui::EventHandler {
    void OnMouseEvent(ui::MouseEvent* event) override { event->SetHandled(); }
  };
  SetHandledHandler handler;
  ash::Shell::Get()->AddPreTargetHandler(&handler);

  Seat seat(std::make_unique<TestDataExchangeDelegate>());
  testing::NiceMock<MockPointerDelegate> pointer_delegate;
  std::unique_ptr<Pointer> pointer(new Pointer(&pointer_delegate, &seat));

  // Make origin into a real window so the touch can click it
  std::unique_ptr<ShellSurface> shell_surface =
      test::ShellSurfaceBuilder({10, 10}).BuildShellSurface();

  EXPECT_CALL(pointer_delegate, CanAcceptPointerEventsForSurface(testing::_))
      .WillRepeatedly(testing::Return(true));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  // The SetHandlerHandler should have marked the event as processed. Therefore
  // the event should simply be ignored.
  EXPECT_CALL(pointer_delegate,
              OnPointerButton(testing::_, testing::_, testing::_))
      .Times(0);

  // This event should be ignored because it has already been handled.
  auto window_point = shell_surface->surface_for_testing()
                          ->window()
                          ->GetBoundsInScreen()
                          .CenterPoint();
  generator.MoveMouseTo(window_point);
  generator.ClickLeftButton();

  ash::Shell::Get()->RemovePreTargetHandler(&handler);
}

namespace {

class PointerDragDropObserver : public WMHelper::DragDropObserver {
 public:
  PointerDragDropObserver(DropCallback closure)
      : closure_(std::move(closure)) {}

 private:
  // WMHelper::DragDropObserver overrides:
  void OnDragEntered(const ui::DropTargetEvent& event) override {}
  aura::client::DragUpdateInfo OnDragUpdated(
      const ui::DropTargetEvent& event) override {
    return aura::client::DragUpdateInfo();
  }
  void OnDragExited() override {}
  DropCallback GetDropCallback() override { return std::move(closure_); }

  DropCallback closure_;
};

}  // namespace

// Test for crbug.com/1307143: It ensures no "pointer enter" event is
// processed in case the target surface is destroyed during the drop action.
TEST_F(PointerTest,
       DragDropAndPointerEnterLeaveEvents_NoEnterOnSurfaceDestroy) {
  Seat seat(std::make_unique<TestDataExchangeDelegate>());
  MockPointerDelegate pointer_delegate;
  std::unique_ptr<Pointer> pointer(new Pointer(&pointer_delegate, &seat));
  TestDataSourceDelegate data_source_delegate;
  DataSource source(&data_source_delegate);
  std::unique_ptr<Surface> origin(new Surface());
  auto* origin_ptr = origin.get();

  // Make origin into a real window so the pointer can click it
  ShellSurface shell_surface(origin_ptr);
  Buffer buffer(exo_test_helper()->CreateGpuMemoryBuffer(gfx::Size(10, 10)));
  origin_ptr->Attach(&buffer);
  origin_ptr->Commit();

  auto closure = base::BindOnce([](std::unique_ptr<Surface> shell_surface,
                                   ui::mojom::DragOperation& output_drag_op) {},
                                std::move(origin));
  PointerDragDropObserver drag_drop_observer(std::move(closure));

  auto* wm_helper = WMHelper::GetInstance();
  wm_helper->AddDragDropObserver(&drag_drop_observer);

  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(pointer_delegate, CanAcceptPointerEventsForSurface(origin_ptr))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(pointer_delegate, OnPointerFrame()).Times(AnyNumber());
  EXPECT_CALL(pointer_delegate, OnPointerEnter(origin_ptr, gfx::PointF(), 0));
  generator.MoveMouseTo(origin_ptr->window()->GetBoundsInScreen().origin());

  auto* drag_drop_controller = static_cast<ash::DragDropController*>(
      aura::client::GetDragDropClient(ash::Shell::GetPrimaryRootWindow()));
  ASSERT_TRUE(drag_drop_controller);

  generator.PressLeftButton();
  seat.StartDrag(&source, origin_ptr, /*icon=*/nullptr,
                 ui::mojom::DragEventSource::kMouse);
  EXPECT_TRUE(seat.get_drag_drop_operation_for_testing());

  // As soon as the runloop gets triggered, emit a mouse release event.
  drag_drop_controller->SetLoopClosureForTesting(
      base::BindLambdaForTesting([&]() {
        EXPECT_CALL(pointer_delegate, OnPointerEnter(_, _, _));
        generator.ReleaseLeftButton();
      }),
      base::DoNothing());

  // OnPointerLeave() gets called twice:
  // 1/ when the drag starts;
  // 2/ when the dragging window gets destroyed.
  EXPECT_CALL(pointer_delegate, OnPointerLeave(_)).Times(2);
  base::RunLoop().RunUntilIdle();

  wm_helper->RemoveDragDropObserver(&drag_drop_observer);

  EXPECT_CALL(pointer_delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

// Test for crbug.com/1307143: It ensures no "pointer enter" event is
// processed in case the target surface parent is destroyed during the drop
// action.
TEST_F(PointerTest,
       DragDropAndPointerEnterLeaveEvents_NoEnterOnParentSurfaceDestroy) {
  Seat seat(std::make_unique<TestDataExchangeDelegate>());
  MockPointerDelegate pointer_delegate;
  std::unique_ptr<Pointer> pointer(new Pointer(&pointer_delegate, &seat));
  TestDataSourceDelegate data_source_delegate;
  DataSource source(&data_source_delegate);

  auto shell_surface = test::ShellSurfaceBuilder({10, 10}).BuildShellSurface();
  auto* surface = shell_surface->surface_for_testing();

  auto closure = base::BindOnce([](std::unique_ptr<ShellSurface> shell_surface,
                                   ui::mojom::DragOperation& output_drag_op) {},
                                std::move(shell_surface));
  PointerDragDropObserver drag_drop_observer(std::move(closure));

  auto* wm_helper = WMHelper::GetInstance();
  wm_helper->AddDragDropObserver(&drag_drop_observer);

  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(pointer_delegate, CanAcceptPointerEventsForSurface(testing::_))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(pointer_delegate, OnPointerFrame()).Times(AnyNumber());
  EXPECT_CALL(pointer_delegate, OnPointerEnter(surface, gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  auto* drag_drop_controller = static_cast<ash::DragDropController*>(
      aura::client::GetDragDropClient(ash::Shell::GetPrimaryRootWindow()));
  ASSERT_TRUE(drag_drop_controller);

  generator.PressLeftButton();
  seat.StartDrag(&source, surface, /*icon=*/nullptr,
                 ui::mojom::DragEventSource::kMouse);
  EXPECT_TRUE(seat.get_drag_drop_operation_for_testing());

  // As soon as the runloop gets triggered, emit a mouse release event.
  drag_drop_controller->SetLoopClosureForTesting(
      base::BindLambdaForTesting([&]() {
        EXPECT_CALL(pointer_delegate, OnPointerEnter(_, _, _)).Times(1);
        generator.ReleaseLeftButton();
      }),
      base::DoNothing());

  // OnPointerLeave() gets called twice:
  // 1/ when the drag starts;
  // 2/ when the dragging window gets destroyed.
  EXPECT_CALL(pointer_delegate, OnPointerLeave(_)).Times(2);
  base::RunLoop().RunUntilIdle();

  wm_helper->RemoveDragDropObserver(&drag_drop_observer);

  EXPECT_CALL(pointer_delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

#endif

TEST_F(PointerTest, OnPointerRelativeMotion) {
  auto surface = std::make_unique<Surface>();
  auto shell_surface = std::make_unique<ShellSurface>(surface.get());
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  MockRelativePointerDelegate relative_delegate;
  Seat seat;
  auto pointer = std::make_unique<Pointer>(&delegate, &seat);
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());
  pointer->RegisterRelativePointerDelegate(&relative_delegate);

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerFrame()).Times(9);

  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(1, 1)));
  EXPECT_CALL(
      relative_delegate,
      OnPointerRelativeMotion(testing::_, gfx::Vector2dF(1, 1), testing::_));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin() +
                        gfx::Vector2d(1, 1));

  EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(2, 2)));
  EXPECT_CALL(
      relative_delegate,
      OnPointerRelativeMotion(testing::_, gfx::Vector2dF(1, 1), testing::_));
  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin() +
                        gfx::Vector2d(2, 2));

  auto sub_surface = std::make_unique<Surface>();
  auto sub = std::make_unique<SubSurface>(sub_surface.get(), surface.get());
  surface->SetSubSurfacePosition(sub_surface.get(), gfx::PointF(5, 5));
  gfx::Size sub_buffer_size(5, 5);
  auto sub_buffer = std::make_unique<Buffer>(
      exo_test_helper()->CreateGpuMemoryBuffer(sub_buffer_size));
  sub_surface->Attach(sub_buffer.get());
  sub_surface->Commit();
  surface->Commit();

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(sub_surface.get()))
      .WillRepeatedly(testing::Return(true));

  EXPECT_CALL(delegate, OnPointerLeave(surface.get()));
  EXPECT_CALL(delegate, OnPointerEnter(sub_surface.get(), gfx::PointF(), 0));
  // OnPointerMotion will not be called, because the pointer location is already
  // sent with OnPointerEnter, but we should still receive
  // OnPointerRelativeMotion.
  EXPECT_CALL(
      relative_delegate,
      OnPointerRelativeMotion(testing::_, gfx::Vector2dF(3, 3), testing::_));
  generator.MoveMouseTo(sub_surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(1, 1)));
  EXPECT_CALL(
      relative_delegate,
      OnPointerRelativeMotion(testing::_, gfx::Vector2dF(1, 1), testing::_));
  generator.MoveMouseTo(sub_surface->window()->GetBoundsInScreen().origin() +
                        gfx::Vector2d(1, 1));

  const gfx::Point child_surface_origin =
      sub_surface->window()->GetBoundsInScreen().origin() +
      gfx::Vector2d(10, 10);
  auto child_surface = std::make_unique<Surface>();
  auto child_shell_surface = std::make_unique<ShellSurface>(
      child_surface.get(), child_surface_origin, /*can_minimize=*/false,
      ash::desks_util::GetActiveDeskContainerId());
  child_shell_surface->DisableMovement();
  child_shell_surface->SetParent(shell_surface.get());
  gfx::Size child_buffer_size(15, 15);
  auto child_buffer = std::make_unique<Buffer>(
      exo_test_helper()->CreateGpuMemoryBuffer(child_buffer_size));
  child_surface->Attach(child_buffer.get());
  child_surface->Commit();

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(child_surface.get()))
      .WillRepeatedly(testing::Return(true));

  EXPECT_CALL(delegate, OnPointerLeave(sub_surface.get()));
  EXPECT_CALL(delegate, OnPointerEnter(child_surface.get(), gfx::PointF(), 0));
  // OnPointerMotion will not be called, because the pointer location is already
  // sent with OnPointerEnter, but we should still receive
  // OnPointerRelativeMotion.
  EXPECT_CALL(
      relative_delegate,
      OnPointerRelativeMotion(testing::_, gfx::Vector2dF(9, 9), testing::_));
  generator.MoveMouseTo(child_surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::PointF(10, 10)));
  EXPECT_CALL(
      relative_delegate,
      OnPointerRelativeMotion(testing::_, gfx::Vector2dF(10, 10), testing::_));
  generator.MoveMouseTo(child_surface->window()->GetBoundsInScreen().origin() +
                        gfx::Vector2d(10, 10));

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  EXPECT_CALL(relative_delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

// TODO(b/161755250): the ifdef is only necessary because of the feature
// flag. This code should work fine on non-cros.
#if BUILDFLAG(IS_CHROMEOS_ASH)
class PointerOrdinalMotionTest : public PointerTest {
 public:
  PointerOrdinalMotionTest() {
    scoped_feature_list_.InitAndEnableFeature(
        chromeos::features::kExoOrdinalMotion);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_F(PointerOrdinalMotionTest, OrdinalMotionOverridesRelativeMotion) {
  auto surface = std::make_unique<Surface>();
  auto shell_surface = std::make_unique<ShellSurface>(surface.get());
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  // Set up the pointer and move it to the origin.
  testing::NiceMock<MockPointerDelegate> delegate;
  Seat seat;
  auto pointer = std::make_unique<Pointer>(&delegate, &seat);
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());
  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  gfx::Point origin = surface->window()->GetBoundsInScreen().origin();
  generator.MoveMouseTo(origin);

  // Start sending relative motion events.
  testing::StrictMock<MockRelativePointerDelegate> relative_delegate;
  pointer->RegisterRelativePointerDelegate(&relative_delegate);

  // By default, ordinal and relative are the same.
  gfx::Point new_location = origin + gfx::Vector2d(1, 1);
  ui::MouseEvent ev1(ui::ET_MOUSE_MOVED, new_location, new_location,
                     ui::EventTimeForNow(), generator.flags(), 0);
  EXPECT_CALL(relative_delegate,
              OnPointerRelativeMotion(testing::_, gfx::Vector2dF(1, 1),
                                      gfx::Vector2dF(1, 1)));
  generator.Dispatch(&ev1);

  // When set, ordinal overrides the relative motion.
  new_location = new_location + gfx::Vector2d(1, 1);
  ui::MouseEvent ev2(ui::ET_MOUSE_MOVED, new_location, new_location,
                     ui::EventTimeForNow(), generator.flags(), 0);
  ui::MouseEvent::DispatcherApi(&ev2).set_movement(gfx::Vector2dF(99, 99));
  EXPECT_CALL(relative_delegate,
              OnPointerRelativeMotion(testing::_, gfx::Vector2dF(1, 1),
                                      gfx::Vector2dF(99, 99)));
  generator.Dispatch(&ev2);

  pointer->UnregisterRelativePointerDelegate(&relative_delegate);
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, ConstrainPointer) {
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame());
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate_, OnPointerMotion(testing::_, testing::_)).Times(0);
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin() +
                          gfx::Vector2d(-1, -1));

  auto child_shell_surface = test::ShellSurfaceBuilder({15, 15})
                                 .SetParent(shell_surface_.get())
                                 .SetDisableMovement()
                                 .SetCanMinimize(false)
                                 .BuildShellSurface();
  Surface* child_surface = child_shell_surface->surface_for_testing();
  EXPECT_CALL(delegate_, CanAcceptPointerEventsForSurface(child_surface))
      .WillRepeatedly(testing::Return(true));

  generator_->MoveMouseTo(
      child_surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate_, OnPointerLeave(surface_));
  EXPECT_CALL(delegate_, OnPointerEnter(child_surface, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame());
  // Moving the cursor to a different surface should change the focus when
  // the pointer is unconstrained.
  pointer_->UnconstrainPointerByUserAction();
  generator_->MoveMouseTo(
      child_surface->window()->GetBoundsInScreen().origin());

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, CanOnlyConstrainPermittedWindows) {
  std::unique_ptr<ShellSurface> shell_surface =
      test::ShellSurfaceBuilder({10, 10}).BuildShellSurface();
  EXPECT_CALL(constraint_delegate_, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(shell_surface->surface_for_testing()));
  // Called once when ConstrainPointer is denied, and again when the delegate
  // is destroyed.
  EXPECT_CALL(constraint_delegate_, OnDefunct()).Times(2);

  EXPECT_FALSE(pointer_->ConstrainPointer(&constraint_delegate_));

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, OneConstraintPerSurface) {
  ON_CALL(constraint_delegate_, IsPersistent())
      .WillByDefault(testing::Return(false));
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame()).Times(testing::AtLeast(1));
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  // Add a second constraint for the same surface, it should fail.
  MockPointerConstraintDelegate second_constraint;
  EXPECT_CALL(second_constraint, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(surface_));
  ON_CALL(second_constraint, IsPersistent())
      .WillByDefault(testing::Return(false));
  EXPECT_CALL(second_constraint, OnAlreadyConstrained());
  EXPECT_CALL(second_constraint, OnDefunct());
  EXPECT_FALSE(pointer_->ConstrainPointer(&second_constraint));

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, OneShotConstraintActivatedOnFirstFocus) {
  auto second_shell_surface = BuildShellSurfaceWhichPermitsPointerLock();
  Surface* second_surface = second_shell_surface->surface_for_testing();

  EXPECT_CALL(delegate_, CanAcceptPointerEventsForSurface(second_surface))
      .WillRepeatedly(testing::Return(true));

  focus_client_->FocusWindow(second_surface->window());

  // Assert: Can no longer activate the constraint on the first surface.
  EXPECT_FALSE(pointer_->ConstrainPointer(&constraint_delegate_));
  EXPECT_EQ(constraint_delegate_.activated_count, 0);

  // Assert: Constraint is activated when first surface gains focus.
  focus_client_->FocusWindow(surface_->window());
  EXPECT_EQ(constraint_delegate_.activated_count, 1);

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame());
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  // Teardown
  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, UnconstrainPointerWhenSurfaceIsDestroyed) {
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame());
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  // Constraint should be broken if surface is destroyed.
  EXPECT_CALL(constraint_delegate_, OnConstraintBroken());
  EXPECT_CALL(delegate_, OnPointerLeave(surface_));
  EXPECT_CALL(delegate_, OnPointerFrame());
  shell_surface_.reset();

  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, UnconstrainPointerWhenWindowLosesFocus) {
  ON_CALL(constraint_delegate_, IsPersistent())
      .WillByDefault(testing::Return(false));
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame());
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(constraint_delegate_, OnConstraintBroken());
  EXPECT_CALL(constraint_delegate_, OnConstraintActivated()).Times(0);
  focus_client_->FocusWindow(nullptr);
  focus_client_->FocusWindow(surface_->window());

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, PersistentConstraintActivatedOnRefocus) {
  ON_CALL(constraint_delegate_, IsPersistent())
      .WillByDefault(testing::Return(true));
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame());
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(constraint_delegate_, OnConstraintBroken());
  focus_client_->FocusWindow(nullptr);
  EXPECT_CALL(constraint_delegate_, OnConstraintActivated());
  focus_client_->FocusWindow(surface_->window());

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, MultipleSurfacesCanBeConstrained) {
  // Arrange: First surface + persistent constraint
  ON_CALL(constraint_delegate_, IsPersistent())
      .WillByDefault(testing::Return(true));
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame());
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  EXPECT_EQ(constraint_delegate_.activated_count, 1);

  // Arrange: Second surface + persistent constraint
  auto second_shell_surface = BuildShellSurfaceWhichPermitsPointerLock();
  Surface* second_surface = second_shell_surface->surface_for_testing();
  focus_client_->FocusWindow(second_surface->window());
  EXPECT_CALL(delegate_, CanAcceptPointerEventsForSurface(second_surface))
      .WillRepeatedly(testing::Return(true));
  testing::NiceMock<MockPointerConstraintDelegate> second_constraint;
  EXPECT_CALL(second_constraint, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(second_surface));
  ON_CALL(second_constraint, IsPersistent())
      .WillByDefault(testing::Return(true));
  EXPECT_TRUE(pointer_->ConstrainPointer(&second_constraint));

  EXPECT_EQ(constraint_delegate_.activated_count, 1);
  EXPECT_EQ(second_constraint.activated_count, 1);

  // Act: Toggle focus, first surface's constraint should activate.
  focus_client_->FocusWindow(surface_->window());

  EXPECT_EQ(constraint_delegate_.activated_count, 2);
  EXPECT_EQ(second_constraint.activated_count, 1);

  // Act: Toggle focus, second surface's constraint should activate.
  focus_client_->FocusWindow(second_surface->window());

  EXPECT_EQ(constraint_delegate_.activated_count, 2);
  EXPECT_EQ(second_constraint.activated_count, 2);

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  pointer_->OnPointerConstraintDelegateDestroying(&second_constraint);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, UserActionPreventsConstraint) {
  ON_CALL(constraint_delegate_, IsPersistent())
      .WillByDefault(testing::Return(false));
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame()).Times(testing::AtLeast(1));
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(constraint_delegate_, OnConstraintBroken());
  pointer_->UnconstrainPointerByUserAction();

  // New constraints are no longer permitted.
  MockPointerConstraintDelegate second_constraint;
  EXPECT_CALL(second_constraint, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(surface_));
  ON_CALL(second_constraint, IsPersistent())
      .WillByDefault(testing::Return(false));
  EXPECT_FALSE(pointer_->ConstrainPointer(&second_constraint));
  EXPECT_EQ(second_constraint.activated_count, 0);

  // A click event will activate the pending constraint.
  generator_->ClickLeftButton();
  EXPECT_EQ(second_constraint.activated_count, 1);

  pointer_->OnPointerConstraintDelegateDestroying(&second_constraint);

  // New constraints are now permitted too.
  MockPointerConstraintDelegate third_constraint;
  EXPECT_CALL(third_constraint, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(surface_));
  ON_CALL(third_constraint, IsPersistent())
      .WillByDefault(testing::Return(false));
  EXPECT_TRUE(pointer_->ConstrainPointer(&third_constraint));
  pointer_->OnPointerConstraintDelegateDestroying(&third_constraint);

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PointerConstraintTest, UserCanBreakAndActivatePersistentConstraint) {
  ON_CALL(constraint_delegate_, IsPersistent())
      .WillByDefault(testing::Return(true));
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));
  EXPECT_EQ(constraint_delegate_.activated_count, 1);
  EXPECT_EQ(constraint_delegate_.broken_count, 0);

  EXPECT_CALL(delegate_, OnPointerEnter(surface_, gfx::PointF(), 0));
  EXPECT_CALL(delegate_, OnPointerFrame()).Times(testing::AtLeast(1));
  generator_->MoveMouseTo(surface_->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(constraint_delegate_, OnConstraintBroken());
  pointer_->UnconstrainPointerByUserAction();
  EXPECT_EQ(constraint_delegate_.activated_count, 1);
  EXPECT_EQ(constraint_delegate_.broken_count, 1);

  // Click events re-enable the constraint.
  generator_->ClickLeftButton();
  EXPECT_EQ(constraint_delegate_.activated_count, 2);

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate_);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));
  pointer_.reset();
}

TEST_F(PointerConstraintTest, DefaultSecurityDeletegate) {
  auto default_security_delegate =
      SecurityDelegate::GetDefaultSecurityDelegate();
  auto shell_surface = test::ShellSurfaceBuilder({10, 10})
                           .SetSecurityDelegate(default_security_delegate.get())
                           .BuildShellSurface();

  auto* surface = shell_surface->surface_for_testing();

  focus_client_->FocusWindow(surface->window());

  MockPointerConstraintDelegate constraint_delegate;

  EXPECT_CALL(constraint_delegate, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(surface));

  EXPECT_CALL(constraint_delegate, OnDefunct()).Times(1);
  EXPECT_FALSE(pointer_->ConstrainPointer(&constraint_delegate));
  ::testing::Mock::VerifyAndClearExpectations(&constraint_delegate);

  shell_surface->GetWidget()->GetNativeWindow()->SetProperty(
      aura::client::kAppType, static_cast<int>(ash::AppType::LACROS));

  EXPECT_CALL(constraint_delegate, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(surface));
  EXPECT_CALL(constraint_delegate, OnDefunct()).Times(0);
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate));

  ::testing::Mock::VerifyAndClearExpectations(&constraint_delegate);

  EXPECT_CALL(constraint_delegate, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(surface));
  shell_surface->GetWidget()->GetNativeWindow()->SetProperty(
      aura::client::kAppType, static_cast<int>(ash::AppType::ARC_APP));
  EXPECT_CALL(constraint_delegate, OnDefunct()).Times(0);
  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate));

  ::testing::Mock::VerifyAndClearExpectations(&constraint_delegate);

  pointer_->OnPointerConstraintDelegateDestroying(&constraint_delegate);
  EXPECT_CALL(delegate_, OnPointerDestroying(pointer_.get()));

  pointer_.reset();
}

TEST_F(PointerConstraintTest, NoPointerMotionEventWhenUnconstrainingPointer) {
  testing::MockFunction<void(std::string check_point_name)> check;
  {
    testing::InSequence s;

    EXPECT_CALL(check, Call("Unconstrain pointer"));
    EXPECT_CALL(delegate_, OnPointerMotion(testing::_, testing::_)).Times(0);
  }

  generator_->MoveMouseTo(
      surface_->window()->GetBoundsInScreen().CenterPoint() +
      gfx::Vector2d(4, 4));

  EXPECT_TRUE(pointer_->ConstrainPointer(&constraint_delegate_));

  generator_->MoveMouseTo(
      surface_->window()->GetBoundsInScreen().CenterPoint() +
      gfx::Vector2d(-4, -4));

  check.Call("Unconstrain pointer");

  pointer_->UnconstrainPointerByUserAction();

  // Ensure the posted task for synthesized mouse move event is run.
  base::RunLoop().RunUntilIdle();

  pointer_.reset();
}

TEST_F(PointerConstraintTest, ConstrainPointerWithUncommittedShellSurface) {
  std::unique_ptr<ShellSurface> uncommitted_shell_surface =
      test::ShellSurfaceBuilder({10, 10}).SetNoCommit().BuildShellSurface();

  Surface* surface = uncommitted_shell_surface->surface_for_testing();
  surface->window()->GetToplevelWindow()->SetProperty(
      chromeos::kUseOverviewToExitPointerLock, true);

  focus_client_->FocusWindow(surface->window());
  EXPECT_CALL(delegate_, CanAcceptPointerEventsForSurface(surface))
      .WillRepeatedly(testing::Return(true));
  testing::NiceMock<MockPointerConstraintDelegate> second_constraint;
  EXPECT_CALL(second_constraint, GetConstrainedSurface())
      .WillRepeatedly(testing::Return(surface));
  ON_CALL(second_constraint, IsPersistent())
      .WillByDefault(testing::Return(true));

  // Verify that the operation doesn't crash.
  // The operation fails because the window associated with |surface| (or its
  // ancestors) cannot be activated before a widget is created in the commit
  // process, while pointer capture is not allowed on an inactive window.
  EXPECT_FALSE(pointer_->ConstrainPointer(&second_constraint));

  pointer_.reset();
}
#endif

TEST_F(PointerTest, PointerStylus) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  gfx::Size buffer_size(10, 10);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  MockPointerStylusDelegate stylus_delegate;
  Seat seat;
  std::unique_ptr<Pointer> pointer(new Pointer(&delegate, &seat));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  pointer->SetStylusDelegate(&stylus_delegate);

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));

  {
    testing::InSequence sequence;
    EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::PointF(), 0));
    EXPECT_CALL(delegate, OnPointerFrame());
    EXPECT_CALL(stylus_delegate,
                OnPointerToolChange(ui::EventPointerType::kMouse));
    EXPECT_CALL(delegate, OnPointerFrame());
  }

  generator.MoveMouseTo(surface->window()->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  EXPECT_CALL(stylus_delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

}  // namespace
}  // namespace exo
