// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.explore_sites;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.recyclerview.widget.RecyclerView;

import org.chromium.chrome.R;
import org.chromium.ui.modelutil.RecyclerViewAdapter;

/** Factory to create CategoryCardViewHolder objects. */
public class CategoryCardViewHolderFactory implements RecyclerViewAdapter.ViewHolderFactory<
        CategoryCardViewHolderFactory.CategoryCardViewHolder> {

    /** View holder for the recycler view. */
    public static class CategoryCardViewHolder extends RecyclerView.ViewHolder {
        public CategoryCardViewHolder(View view) {
            super(view);
        }
    }

    /** Override this method to change the resource that is used for category cards. */
    protected int getCategoryCardViewResource() {
        return R.layout.explore_sites_category_card_view;
    }

    /**
     * Override this method to change the resource that is used for site tiles within the category
     * cards.
     */
    protected int getTileViewResource() {
        return R.layout.explore_sites_tile_view;
    }

    @Override
    public CategoryCardViewHolder createViewHolder(
            ViewGroup parent, @CategoryCardAdapter.ViewType int viewType) {
        View view;
        switch (viewType) {
            case CategoryCardAdapter.ViewType.CATEGORY:
                ExploreSitesCategoryCardView category =
                        (ExploreSitesCategoryCardView) LayoutInflater.from(parent.getContext())
                                .inflate(getCategoryCardViewResource(), parent,
                                        /* attachToRoot = */ false);
                category.setTileResource(getTileViewResource());
                view = category;
                break;
            case CategoryCardAdapter.ViewType.LOADING:
                view = LayoutInflater.from(parent.getContext())
                               .inflate(R.layout.explore_sites_loading_from_net_view, parent,
                                       /* attachToRoot = */ false);
                break;
            case CategoryCardAdapter.ViewType.ERROR:
                view = LayoutInflater.from(parent.getContext())
                               .inflate(R.layout.explore_sites_loading_error_view, parent,
                                       /* attachToRoot = */ false);
                break;
            default:
                assert false;
                view = null;
        }
        return new CategoryCardViewHolder(view);
    }
}