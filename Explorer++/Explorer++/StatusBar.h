// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "../Helper/WeakPtrFactory.h"
#include <boost/signals2.hpp>
#include <concurrencpp/concurrencpp.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class BrowserWindow;
struct Config;
class NavigationEvents;
class NavigationRequest;
class ResourceLoader;
class Runtime;
class ShellBrowser;
class ShellBrowserEvents;
class StatusBarView;
class Tab;
class TabEvents;

class StatusBar
{
public:
	static StatusBar *Create(StatusBarView *view, const BrowserWindow *browser,
		const Config *config, TabEvents *tabEvents, ShellBrowserEvents *shellBrowserEvents,
		NavigationEvents *navigationEvents, const ResourceLoader *resourceLoader,
		const Runtime *runtime);

	StatusBarView *GetView();

	void NotifyMenuLoopStarted();
	void NotifyMenuLoopEnded();
	void OnMenuSelect(HMENU menu, UINT itemId, UINT flags);

private:
	StatusBar(StatusBarView *view, const BrowserWindow *browser, const Config *config,
		TabEvents *tabEvents, ShellBrowserEvents *shellBrowserEvents,
		NavigationEvents *navigationEvents, const ResourceLoader *resourceLoader,
		const Runtime *runtime);

	void SetStandardParts();

	void OnTabSelected(const Tab &tab);
	void OnDirectoryContentsChanged(const ShellBrowser *shellBrowser);
	void OnListViewSelectionChanged(const ShellBrowser *shellBrowser);
	void UpdateTextForNavigation(const NavigationRequest *request);
	void OnNavigationsStopped(const ShellBrowser *shellBrowser);

	void UpdateText(const Tab &tab);
	void SetLoadingText(PCIDLIST_ABSOLUTE pidl);
	void UpdateDriveFreeSpace(const Tab &tab);
	static concurrencpp::null_result FetchDriveFreeSpace(WeakPtr<StatusBar> self,
		const Runtime *runtime, int tabId, std::wstring path, uint64_t generation);

	void OnWindowDestroyed();

	StatusBarView *const m_view;
	const BrowserWindow *const m_browser;
	const Config *const m_config;
	const ResourceLoader *const m_resourceLoader;
	const Runtime *const m_runtime;
	bool m_showingMenuHelpText = false;
	bool m_driveRequestPending = false;
	uint64_t m_driveGeneration = 0;
	std::optional<std::wstring> m_cachedDrivePath;
	std::wstring m_cachedDriveText;
	WeakPtrFactory<StatusBar> m_weakPtrFactory{ this };
	std::vector<boost::signals2::scoped_connection> m_connections;
};
