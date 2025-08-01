/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.appmenu;

import static org.chromium.build.NullUtil.assertNonNull;
import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.text.TextUtils;
import android.view.Menu;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.View;
import android.widget.ImageButton;

import androidx.annotation.NonNull;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.core.content.ContextCompat;
import androidx.core.graphics.drawable.DrawableCompat;

import org.chromium.base.BraveFeatureList;
import org.chromium.base.BravePreferenceKeys;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.brave_vpn.mojom.BraveVpnConstants;
import org.chromium.build.annotations.MonotonicNonNull;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ActivityTabProvider;
import org.chromium.chrome.browser.BraveRewardsNativeWorker;
import org.chromium.chrome.browser.app.appmenu.AppMenuIconRowFooter;
import org.chromium.chrome.browser.bookmarks.BookmarkModel;
import org.chromium.chrome.browser.brave_leo.BraveLeoPrefUtils;
import org.chromium.chrome.browser.feed.webfeed.WebFeedSnackbarController;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.homepage.HomepageManager;
import org.chromium.chrome.browser.incognito.reauth.IncognitoReauthController;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.multiwindow.BraveMultiWindowUtils;
import org.chromium.chrome.browser.multiwindow.MultiWindowModeStateDispatcher;
import org.chromium.chrome.browser.preferences.BravePref;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.readaloud.ReadAloudController;
import org.chromium.chrome.browser.set_default_browser.BraveSetDefaultBrowserUtils;
import org.chromium.chrome.browser.speedreader.BraveSpeedReaderUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabbed_mode.TabbedAppMenuPropertiesDelegate;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.toolbar.ToolbarManager;
import org.chromium.chrome.browser.toolbar.bottom.BottomToolbarConfiguration;
import org.chromium.chrome.browser.toolbar.menu_button.BraveMenuButtonCoordinator;
import org.chromium.chrome.browser.toolbar.top.BraveToolbarLayoutImpl;
import org.chromium.chrome.browser.ui.appmenu.AppMenuDelegate;
import org.chromium.chrome.browser.ui.appmenu.AppMenuHandler;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.vpn.utils.BraveVpnPrefUtils;
import org.chromium.chrome.browser.vpn.utils.BraveVpnProfileUtils;
import org.chromium.chrome.browser.vpn.utils.BraveVpnUtils;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.ui.modaldialog.ModalDialogManager;

/** Brave's extension for TabbedAppMenuPropertiesDelegate */
@NullMarked
public class BraveTabbedAppMenuPropertiesDelegate extends TabbedAppMenuPropertiesDelegate {
    private @MonotonicNonNull Menu mMenu;
    private final AppMenuDelegate mAppMenuDelegate;
    private final ObservableSupplier<BookmarkModel> mBookmarkModelSupplier;

    public BraveTabbedAppMenuPropertiesDelegate(
            Context context,
            ActivityTabProvider activityTabProvider,
            MultiWindowModeStateDispatcher multiWindowModeStateDispatcher,
            TabModelSelector tabModelSelector,
            ToolbarManager toolbarManager,
            View decorView,
            AppMenuDelegate appMenuDelegate,
            OneshotSupplier<LayoutStateProvider> layoutStateProvider,
            ObservableSupplier<BookmarkModel> bookmarkModelSupplier,
            WebFeedSnackbarController.FeedLauncher feedLauncher,
            ModalDialogManager modalDialogManager,
            SnackbarManager snackbarManager,
            @NonNull
                    OneshotSupplier<IncognitoReauthController>
                            incognitoReauthControllerOneshotSupplier,
            Supplier<ReadAloudController> readAloudControllerSupplier) {
        super(
                context,
                activityTabProvider,
                multiWindowModeStateDispatcher,
                tabModelSelector,
                toolbarManager,
                decorView,
                appMenuDelegate,
                layoutStateProvider,
                bookmarkModelSupplier,
                feedLauncher,
                modalDialogManager,
                snackbarManager,
                incognitoReauthControllerOneshotSupplier,
                readAloudControllerSupplier);

        mAppMenuDelegate = appMenuDelegate;
        mBookmarkModelSupplier = bookmarkModelSupplier;
    }

    @Override
    public void prepareMenu(Menu menu, AppMenuHandler handler) {
        super.prepareMenu(menu, handler);

        maybeReplaceIcons(menu);

        mMenu = menu;

        if (BraveVpnUtils.isVpnFeatureSupported(mContext)) {
            SubMenu vpnSubMenu = menu.findItem(R.id.request_brave_vpn_row_menu_id).getSubMenu();
            if (vpnSubMenu == null) {
                throw new NullPointerException("Unexpected null vpnSubMenu");
            }
            MenuItem braveVpnSubMenuItem = vpnSubMenu.findItem(R.id.request_brave_vpn_id);
            if (braveVpnSubMenuItem == null) {
                throw new NullPointerException("Unexpected null braveVpnSubMenuItem");
            }

            if (shouldShowIconBeforeItem()) {
                braveVpnSubMenuItem.setIcon(
                        AppCompatResources.getDrawable(mContext, R.drawable.ic_vpn));
            }
            MenuItem braveVpnCheckedSubMenuItem =
                    vpnSubMenu.findItem(R.id.request_brave_vpn_check_id);
            if (braveVpnCheckedSubMenuItem != null) {
                braveVpnCheckedSubMenuItem.setCheckable(true);
                braveVpnCheckedSubMenuItem.setChecked(
                        BraveVpnProfileUtils.getInstance().isBraveVPNConnected(mContext));
            }

            if (BraveVpnPrefUtils.isSubscriptionPurchase()
                    && !TextUtils.isEmpty(BraveVpnPrefUtils.getRegionIsoCode())) {
                String serverLocation = " %s  %s - %s";
                SubMenu vpnLocationSubMenu =
                        menu.findItem(R.id.request_vpn_location_row_menu_id).getSubMenu();
                if (vpnLocationSubMenu == null) {
                    throw new NullPointerException("Unexpected null vpnLocationSubMenu");
                }
                MenuItem vpnLocationSubMenuItem =
                        vpnLocationSubMenu.findItem(R.id.request_vpn_location_id);
                String regionName =
                        BraveVpnPrefUtils.getRegionPrecision()
                                        .equals(BraveVpnConstants.REGION_PRECISION_COUNTRY)
                                ? mContext.getString(R.string.optimal_text)
                                : BraveVpnPrefUtils.getRegionNamePretty();

                vpnLocationSubMenuItem.setTitle(
                        String.format(
                                serverLocation,
                                BraveVpnUtils.countryCodeToEmoji(
                                        BraveVpnPrefUtils.getRegionIsoCode()),
                                BraveVpnPrefUtils.getRegionIsoCode(),
                                regionName));
                MenuItem vpnLocationIconSubMenuItem =
                        vpnLocationSubMenu.findItem(R.id.request_vpn_location_icon_id);
                Drawable drawable = vpnLocationIconSubMenuItem.getIcon();
                if (drawable == null) {
                    throw new NullPointerException("Unexpected null drawable");
                }

                drawable = DrawableCompat.wrap(drawable);
                DrawableCompat.setTint(
                        drawable, ContextCompat.getColor(mContext, R.color.vpn_timer_icon_color));
                vpnLocationIconSubMenuItem.setIcon(drawable);
            } else {
                menu.findItem(R.id.request_vpn_location_row_menu_id).setVisible(false);
            }
        } else {
            menu.findItem(R.id.request_brave_vpn_row_menu_id).setVisible(false);
            menu.findItem(R.id.request_vpn_location_row_menu_id).setVisible(false);
        }

        // Brave donesn't show `Clear browsing data` menu.
        menu.findItem(R.id.quick_delete_menu_id).setVisible(false).setEnabled(false);
        menu.findItem(R.id.quick_delete_divider_line_id).setVisible(false).setEnabled(false);

        // Brave's items are only visible for page menu.
        // To make logic simple, below three items are added whenever menu gets visible
        // and removed when menu is dismissed.

        if (!shouldShowPageMenu()) return;

        if (isMenuButtonInBottomToolbar()) {
            // Do not show icon row on top when menu itself is on bottom
            menu.findItem(R.id.icon_row_menu_id).setVisible(false).setEnabled(false);
        }

        // Brave donesn't show help menu item in app menu.
        menu.findItem(R.id.help_id).setVisible(false).setEnabled(false);

        // Always hide share row menu item in app menu if it's not on tablet.
        if (!mIsTablet) menu.findItem(R.id.share_row_menu_id).setVisible(false);

        MenuItem setAsDefault = menu.findItem(R.id.set_default_browser);
        if (shouldShowIconBeforeItem()) {
            setAsDefault.setIcon(
                    AppCompatResources.getDrawable(mContext, R.drawable.brave_menu_set_as_default));
        }

        BraveRewardsNativeWorker braveRewardsNativeWorker = BraveRewardsNativeWorker.getInstance();
        if (braveRewardsNativeWorker != null && braveRewardsNativeWorker.isSupported()) {
            MenuItem rewards =
                    menu.add(Menu.NONE, R.id.brave_rewards_id, 0, R.string.menu_brave_rewards);
            if (shouldShowIconBeforeItem()) {
                rewards.setIcon(
                        AppCompatResources.getDrawable(mContext, R.drawable.brave_menu_rewards));
            }
        }
        MenuItem braveWallet = menu.findItem(R.id.brave_wallet_id);
        if (braveWallet != null) {
            if (ChromeFeatureList.isEnabled(BraveFeatureList.NATIVE_BRAVE_WALLET)) {
                braveWallet.setVisible(true);
                if (shouldShowIconBeforeItem()) {
                    braveWallet.setIcon(
                            AppCompatResources.getDrawable(mContext, R.drawable.ic_crypto_wallets));
                }
            } else {
                braveWallet.setVisible(false);
            }
        }
        MenuItem braveLeo = menu.findItem(R.id.brave_leo_id);
        if (braveLeo != null) {
            Tab tab = mActivityTabProvider.get();
            if (BraveLeoPrefUtils.isLeoEnabled() && tab != null && !tab.isIncognito()) {
                braveLeo.setVisible(true);
                if (shouldShowIconBeforeItem()) {
                    braveLeo.setIcon(
                            AppCompatResources.getDrawable(mContext, R.drawable.ic_brave_ai));
                }
            } else {
                braveLeo.setVisible(false);
            }
        }

        MenuItem bravePlaylist = menu.findItem(R.id.brave_playlist_id);
        if (bravePlaylist != null) {
            if (ChromeFeatureList.isEnabled(BraveFeatureList.BRAVE_PLAYLIST)
                    && ChromeSharedPreferences.getInstance()
                            .readBoolean(BravePreferenceKeys.PREF_ENABLE_PLAYLIST, true)) {
                bravePlaylist.setVisible(true);
                if (shouldShowIconBeforeItem()) {
                    bravePlaylist.setIcon(
                            AppCompatResources.getDrawable(mContext, R.drawable.ic_open_playlist));
                }
            } else {
                bravePlaylist.setVisible(false);
            }
        }

        MenuItem addToPlaylist = menu.findItem(R.id.add_to_playlist_id);
        if (addToPlaylist != null) {
            if (ChromeFeatureList.isEnabled(BraveFeatureList.BRAVE_PLAYLIST)
                    && ChromeSharedPreferences.getInstance()
                            .readBoolean(BravePreferenceKeys.PREF_ENABLE_PLAYLIST, true)
                    && !ChromeSharedPreferences.getInstance()
                            .readBoolean(BravePreferenceKeys.PREF_ADD_TO_PLAYLIST_BUTTON, true)
                    && BraveToolbarLayoutImpl.mShouldShowPlaylistMenu) {
                addToPlaylist.setVisible(true);
                if (shouldShowIconBeforeItem()) {
                    addToPlaylist.setIcon(
                            AppCompatResources.getDrawable(
                                    mContext, R.drawable.ic_baseline_add_24));
                }
            } else {
                addToPlaylist.setVisible(false);
            }
        }

        MenuItem braveNews = menu.add(Menu.NONE, R.id.brave_news_id, 0, R.string.brave_news_title);
        if (shouldShowIconBeforeItem()) {
            braveNews.setIcon(AppCompatResources.getDrawable(mContext, R.drawable.ic_news));
        }

        MenuItem braveSpeedReader = menu.findItem(R.id.brave_speedreader_id);
        braveSpeedReader.setVisible(false);
        if (ChromeFeatureList.isEnabled(BraveFeatureList.BRAVE_SPEEDREADER)
                && UserPrefs.get(assumeNonNull(mTabModelSelector.getCurrentModel().getProfile()))
                        .getBoolean(BravePref.SPEEDREADER_PREF_ENABLED)) {
            final Tab currentTab = mActivityTabProvider.get();
            if (currentTab != null && BraveSpeedReaderUtils.tabSupportsDistillation(currentTab)) {
                braveSpeedReader.setVisible(true);
                if (shouldShowIconBeforeItem()) {
                    braveSpeedReader.setIcon(
                            AppCompatResources.getDrawable(mContext, R.drawable.ic_readermode));
                }
            }
        }

        MenuItem exit = menu.add(Menu.NONE, R.id.exit_id, 0, R.string.menu_exit);
        if (shouldShowIconBeforeItem()) {
            exit.setIcon(AppCompatResources.getDrawable(mContext, R.drawable.brave_menu_exit));
        }

        if (BraveSetDefaultBrowserUtils.isBraveSetAsDefaultBrowser(mContext)) {
            menu.findItem(R.id.set_default_browser).setVisible(false);
        }

        Tab currentTab = mActivityTabProvider.get();

        // Replace info item with share
        MenuItem shareItem = menu.findItem(R.id.info_menu_id);
        if (shareItem != null) {
            shareItem.setTitle(mContext.getString(R.string.share));
            shareItem.setIcon(AppCompatResources.getDrawable(mContext, R.drawable.share_icon));
            if (currentTab != null && UrlUtilities.isNtpUrl(currentTab.getUrl().getSpec())) {
                shareItem.setEnabled(false);
            }
        }

        // By this we forcibly initialize BookmarkBridge
        MenuItem bookmarkItem = menu.findItem(R.id.bookmark_this_page_id);
        if (bookmarkItem != null && currentTab != null) {
            updateBookmarkMenuItemShortcut(bookmarkItem, currentTab, /* fromCct= */ false);
        }

        // Remove unused dividers. This needs to be done after the visibility of all the items is
        // set.
        boolean hasItemBetweenDividers = false;
        for (int i = 0; i < menu.size(); ++i) {
            MenuItem item = menu.getItem(i);
            if (item.getItemId() == R.id.divider_line_id) {
                if (!hasItemBetweenDividers) {
                    // If there isn't any visible menu items between the two divider lines, mark
                    // this line invisible.
                    item.setVisible(false);
                } else {
                    hasItemBetweenDividers = false;
                }
            } else if (!hasItemBetweenDividers && item.isVisible()) {
                // When the item isn't a divider line and is visible, we set hasItemBetweenDividers
                // to be true.
                hasItemBetweenDividers = true;
            }
        }
    }

    @Override
    public void onMenuDismissed() {
        super.onMenuDismissed();

        assertNonNull(mMenu);

        mMenu.removeItem(R.id.set_default_browser);
        mMenu.removeItem(R.id.brave_rewards_id);
        mMenu.removeItem(R.id.brave_wallet_id);
        mMenu.removeItem(R.id.brave_playlist_id);
        mMenu.removeItem(R.id.add_to_playlist_id);
        mMenu.removeItem(R.id.brave_speedreader_id);
        mMenu.removeItem(R.id.exit_id);
        mMenu.removeItem(R.id.request_brave_vpn_row_menu_id);
        mMenu.removeItem(R.id.request_vpn_location_id);
    }

    @Override
    public void onFooterViewInflated(AppMenuHandler appMenuHandler, View view) {
        // If it's still null, just hide the whole view
        if (mBookmarkModelSupplier.get() == null) {
            if (view != null) {
                view.setVisibility(View.GONE);
            }
            // Normally it should not happen
            assert false;
            return;
        }
        super.onFooterViewInflated(appMenuHandler, view);

        if (view instanceof AppMenuIconRowFooter) {
            ((AppMenuIconRowFooter) view)
                    .initialize(appMenuHandler, mBookmarkModelSupplier.get(),
                            mActivityTabProvider.get(), mAppMenuDelegate);
        }

        // Hide bookmark button if bottom toolbar is enabled and address bar is on top.
        ImageButton bookmarkButton = view.findViewById(R.id.bookmark_this_page_id);
        if (bookmarkButton != null
                && BottomToolbarConfiguration.isBraveBottomControlsEnabled()
                && BottomToolbarConfiguration.isToolbarTopAnchored()) {
            bookmarkButton.setVisibility(View.GONE);
        }

        boolean showForwardButton = BottomToolbarConfiguration.isToolbarTopAnchored();
        ImageButton forwardButton = view.findViewById(R.id.forward_menu_id);
        if (forwardButton != null) {
            showForwardButton = showForwardButton || forwardButton.isEnabled();
            forwardButton.setVisibility(showForwardButton ? View.VISIBLE : View.GONE);
        }

        ImageButton shareButton = view.findViewById(R.id.share_menu_id);
        boolean showShareButton =
                BottomToolbarConfiguration.isToolbarTopAnchored() || !showForwardButton;
        if (shareButton != null) {
            Tab currentTab = mActivityTabProvider.get();
            if (currentTab != null && UrlUtilities.isNtpUrl(currentTab.getUrl().getSpec())) {
                shareButton.setEnabled(false);
            }
            shareButton.setVisibility(showShareButton ? View.VISIBLE : View.GONE);
        }

        ImageButton homeButton = view.findViewById(R.id.home_menu_id);
        if (homeButton != null && HomepageManager.getInstance() != null) {
            homeButton.setVisibility(
                    BottomToolbarConfiguration.isToolbarBottomAnchored()
                                    && HomepageManager.getInstance().isHomepageEnabled()
                            ? View.VISIBLE
                            : View.GONE);
        }
    }

    @Override
    public boolean shouldShowHeader(int maxMenuHeight) {
        if (isMenuButtonInBottomToolbar()) return false;
        return super.shouldShowHeader(maxMenuHeight);
    }

    @Override
    public boolean shouldShowFooter(int maxMenuHeight) {
        if (isMenuButtonInBottomToolbar()) return true;
        return super.shouldShowFooter(maxMenuHeight);
    }

    @Override
    public int getFooterResourceId() {
        if (isMenuButtonInBottomToolbar()) {
            return shouldShowPageMenu() ? R.layout.icon_row_menu_footer : 0;
        }
        return super.getFooterResourceId();
    }

    private boolean isMenuButtonInBottomToolbar() {
        return BraveMenuButtonCoordinator.isMenuFromBottom();
    }

    private void maybeReplaceIcons(@Nullable Menu menu) {
        if (menu != null && shouldShowIconBeforeItem()) {
            for (int i = 0; i < menu.size(); ++i) {
                MenuItem item = menu.getItem(i);
                if (item.hasSubMenu()) {
                    maybeReplaceIcons(item.getSubMenu());
                } else if (item.getItemId() == R.id.new_tab_menu_id) {
                    item.setIcon(
                            AppCompatResources.getDrawable(mContext, R.drawable.ic_new_tab_page));
                } else if (item.getItemId() == R.id.new_incognito_tab_menu_id) {
                    item.setIcon(
                            AppCompatResources.getDrawable(
                                    mContext, R.drawable.brave_menu_new_private_tab));
                } else if (item.getItemId() == R.id.all_bookmarks_menu_id) {
                    item.setIcon(
                            AppCompatResources.getDrawable(
                                    mContext, R.drawable.brave_menu_bookmarks));
                } else if (item.getItemId() == R.id.recent_tabs_menu_id) {
                    item.setIcon(
                            AppCompatResources.getDrawable(
                                    mContext, R.drawable.brave_menu_recent_tabs));
                } else if (item.getItemId() == R.id.open_history_menu_id) {
                    item.setIcon(
                            AppCompatResources.getDrawable(
                                    mContext, R.drawable.brave_menu_history));
                } else if (item.getItemId() == R.id.downloads_menu_id) {
                    item.setIcon(
                            AppCompatResources.getDrawable(
                                    mContext, R.drawable.brave_menu_downloads));
                } else if (item.getItemId() == R.id.preferences_id) {
                    item.setIcon(
                            AppCompatResources.getDrawable(
                                    mContext, R.drawable.brave_menu_settings));
                } else if (item.getItemId() == R.id.download_page_id) {
                    item.setIcon(AppCompatResources.getDrawable(mContext, R.drawable.ic_download));
                }
            }
        }
    }

    @Override
    public int getAppMenuLayoutId() {
        return R.menu.brave_main_menu;
    }

    @Override
    public boolean shouldShowNewWindow() {
        return BraveMultiWindowUtils.shouldEnableMultiWindows() && super.shouldShowNewWindow();
    }

    @Override
    protected boolean shouldShowMoveToOtherWindow() {
        return BraveMultiWindowUtils.shouldEnableMultiWindows()
                && super.shouldShowMoveToOtherWindow();
    }
}
