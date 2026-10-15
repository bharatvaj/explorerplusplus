// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "StatusBar.h"
#include "BrowserWindow.h"
#include "Config.h"
#include "MainResource.h"
#include "ResourceLoader.h"
#include "RuntimeHelper.h"
#include "ShellBrowser/NavigationEvents.h"
#include "ShellBrowser/ShellBrowserEvents.h"
#include "ShellBrowser/ShellBrowserImpl.h"
#include "StatusBarView.h"
#include "TabEvents.h"
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <wil/common.h>
#include <wil/resource.h>
#include <format>

StatusBar *StatusBar::Create(StatusBarView *view, const BrowserWindow *browser,
	const Config *config, TabEvents *tabEvents, ShellBrowserEvents *shellBrowserEvents,
	NavigationEvents *navigationEvents, const ResourceLoader *resourceLoader,
	const Runtime *runtime)
{
	return new StatusBar(view, browser, config, tabEvents, shellBrowserEvents, navigationEvents,
		resourceLoader, runtime);
}

StatusBar::StatusBar(StatusBarView *view, const BrowserWindow *browser, const Config *config,
	TabEvents *tabEvents, ShellBrowserEvents *shellBrowserEvents,
	NavigationEvents *navigationEvents, const ResourceLoader *resourceLoader,
	const Runtime *runtime) :
	m_view(view),
	m_browser(browser),
	m_config(config),
	m_resourceLoader(resourceLoader),
	m_runtime(runtime)
{
	SetStandardParts();

	m_view->windowDestroyedSignal.AddObserver(std::bind_front(&StatusBar::OnWindowDestroyed, this));

	m_connections.push_back(tabEvents->AddSelectedObserver(
		std::bind_front(&StatusBar::OnTabSelected, this), TabEventScope::ForBrowser(*browser)));

	m_connections.push_back(shellBrowserEvents->AddItemsChangedObserver(
		std::bind_front(&StatusBar::OnDirectoryContentsChanged, this),
		ShellBrowserEventScope::ForActiveShellBrowser(*browser)));
	m_connections.push_back(shellBrowserEvents->AddSelectionChangedObserver(
		std::bind_front(&StatusBar::OnListViewSelectionChanged, this),
		ShellBrowserEventScope::ForActiveShellBrowser(*browser)));

	m_connections.push_back(navigationEvents->AddStartedObserver(
		std::bind_front(&StatusBar::UpdateTextForNavigation, this),
		NavigationEventScope::ForActiveShellBrowser(*browser)));
	m_connections.push_back(navigationEvents->AddCommittedObserver(
		std::bind_front(&StatusBar::UpdateTextForNavigation, this),
		NavigationEventScope::ForActiveShellBrowser(*browser)));
	m_connections.push_back(navigationEvents->AddFailedObserver(
		std::bind_front(&StatusBar::UpdateTextForNavigation, this),
		NavigationEventScope::ForActiveShellBrowser(*browser)));
	m_connections.push_back(navigationEvents->AddCancelledObserver(
		std::bind_front(&StatusBar::UpdateTextForNavigation, this),
		NavigationEventScope::ForActiveShellBrowser(*browser)));
	m_connections.push_back(navigationEvents->AddStoppedObserver(
		std::bind_front(&StatusBar::OnNavigationsStopped, this),
		NavigationEventScope::ForActiveShellBrowser(*browser)));
}

void StatusBar::SetStandardParts()
{
	m_view->SetParts({ 50, 75, 100 });
}

void StatusBar::OnTabSelected(const Tab &tab)
{
	++m_driveGeneration;
	m_cachedDrivePath.reset();
	UpdateText(tab);
}

void StatusBar::OnDirectoryContentsChanged(const ShellBrowser *shellBrowser)
{
	// A failed lookup must not be retried for every notification from an unavailable share.
	if (!m_driveRequestPending && !m_cachedDriveText.empty())
	{
		m_cachedDrivePath.reset();
	}
	const auto *tab = shellBrowser->GetTab();
	UpdateText(*tab);
}

void StatusBar::OnListViewSelectionChanged(const ShellBrowser *shellBrowser)
{
	const auto *tab = shellBrowser->GetTab();
	UpdateText(*tab);
}

void StatusBar::UpdateTextForNavigation(const NavigationRequest *request)
{
	++m_driveGeneration;
	m_cachedDrivePath.reset();
	const auto *tab = request->GetShellBrowser()->GetTab();
	UpdateText(*tab);
}

void StatusBar::OnNavigationsStopped(const ShellBrowser *shellBrowser)
{
	const auto *tab = shellBrowser->GetTab();
	UpdateText(*tab);
}

StatusBarView *StatusBar::GetView()
{
	return m_view;
}

void StatusBar::NotifyMenuLoopStarted()
{
	++m_driveGeneration;
	m_showingMenuHelpText = true;

	m_view->SetParts({ 100 });
	m_view->SetPartText(0, L"");
}

void StatusBar::NotifyMenuLoopEnded()
{
	m_showingMenuHelpText = false;

	const auto *tab = m_browser->GetActiveShellBrowser()->GetTab();
	SetStandardParts();
	UpdateText(*tab);
}

void StatusBar::OnMenuSelect(HMENU menu, UINT itemId, UINT flags)
{
	if (!menu)
	{
		return;
	}

	std::optional<std::wstring> helpText;

	if (WI_AreAllFlagsClear(flags, MF_POPUP | MF_SEPARATOR))
	{
		helpText = m_browser->RequestMenuHelpText(menu, itemId);
	}

	m_view->SetPartText(0, helpText.value_or(L""));
}

void StatusBar::UpdateText(const Tab &tab)
{
	if (m_showingMenuHelpText)
	{
		// If menu help text is being shown, there's no need to update the status bar text for the
		// current tab.
		return;
	}

	if (auto *navigation = tab.GetShellBrowser()->MaybeGetLatestActiveNavigation())
	{
		// In this case, there is at least one active navigation in progress, so the status bar
		// should reflect that.
		SetLoadingText(navigation->GetNavigateParams().pidl.Raw());
		return;
	}

	int numItemsSelected = tab.GetShellBrowserImpl()->GetNumSelected();
	std::wstring numItemsText;

	// The item count that's shown will either be the number of items selected, or the total number
	// of items if there is no selection.
	if (numItemsSelected != 0)
	{
		if (numItemsSelected == 1)
		{
			numItemsText = m_resourceLoader->LoadString(IDS_GENERAL_SELECTED_ONE_ITEM);
		}
		else
		{
			auto multipleItemsText =
				m_resourceLoader->LoadString(IDS_GENERAL_SELECTED_MULTIPLE_ITEMS);
			numItemsText = std::format(L"{:L} {}", numItemsSelected, multipleItemsText);
		}
	}
	else
	{
		int numItems = tab.GetShellBrowserImpl()->GetNumItems();

		if (numItems == 1)
		{
			numItemsText = m_resourceLoader->LoadString(IDS_GENERAL_ONE_ITEM);
		}
		else
		{
			auto multipleItemsText = m_resourceLoader->LoadString(IDS_GENERAL_MULTIPLE_ITEMS);
			numItemsText = std::format(L"{:L} {}", numItems, multipleItemsText);
		}
	}

	if (tab.GetShellBrowser()->IsFilterEnabled())
	{
		auto filterAppliedText = m_resourceLoader->LoadString(IDS_FILTER_ENABLED);
		numItemsText += L" | " + filterAppliedText;
	}

	m_view->SetPartText(0, numItemsText);

	std::wstring sizeText;

	if (numItemsSelected == 0)
	{
		auto displayFormat = m_config->globalFolderSettings.forceSize
			? m_config->globalFolderSettings.sizeDisplayFormat
			: +SizeDisplayFormat::None;
		sizeText =
			FormatSizeString(tab.GetShellBrowserImpl()->GetTotalDirectorySize(), displayFormat);
	}
	else
	{
		// Note that no size will be shown if only folders are selected.
		if (tab.GetShellBrowserImpl()->GetNumSelectedFiles() != 0)
		{
			auto displayFormat = m_config->globalFolderSettings.forceSize
				? m_config->globalFolderSettings.sizeDisplayFormat
				: +SizeDisplayFormat::None;
			sizeText =
				FormatSizeString(tab.GetShellBrowserImpl()->GetSelectionSize(), displayFormat);
		}
	}

	m_view->SetPartText(1, sizeText);

	UpdateDriveFreeSpace(tab);
}

void StatusBar::SetLoadingText(PCIDLIST_ABSOLUTE pidl)
{
	std::wstring loadingTemplate = m_resourceLoader->LoadString(IDS_GENERAL_LOADING);
	std::wstring loadingText = fmt::format(fmt::runtime(loadingTemplate),
		fmt::arg(L"folder_name", GetDisplayNameWithFallback(pidl, SHGDN_INFOLDER)));
	m_view->SetPartText(0, loadingText);
	m_view->SetPartText(1, L"");
	m_view->SetPartText(2, L"");
}

void StatusBar::UpdateDriveFreeSpace(const Tab &tab)
{
	auto path = tab.GetShellBrowser()->GetDirectoryPath();
	m_view->SetPartText(2, m_cachedDrivePath == path ? m_cachedDriveText : L"");

	if (path.empty() || m_cachedDrivePath == path || m_driveRequestPending)
	{
		return;
	}

	// Only one potentially slow network lookup may be queued at a time. When it finishes,
	// the active tab is checked again so a rapid series of changes only requests the latest path.
	m_driveRequestPending = true;
	FetchDriveFreeSpace(m_weakPtrFactory.GetWeakPtr(), m_runtime, tab.GetId(), std::move(path),
		m_driveGeneration);
}

concurrencpp::null_result StatusBar::FetchDriveFreeSpace(WeakPtr<StatusBar> self,
	const Runtime *runtime, int tabId, std::wstring path, uint64_t generation)
{
	co_await ResumeOnComStaThread(runtime);

	ULARGE_INTEGER totalBytes;
	ULARGE_INTEGER freeBytes;
	bool hasSpace = GetDiskFreeSpaceEx(path.c_str(), nullptr, &totalBytes, &freeBytes);

	co_await ResumeOnUiThread(runtime);

	if (!self)
	{
		co_return;
	}

	self->m_driveRequestPending = false;
	if (!self->m_showingMenuHelpText
		&& self->m_browser->GetLifecycleState() == BrowserWindow::LifecycleState::Main)
	{
		const auto *activeBrowser = self->m_browser->GetActiveShellBrowser();
		const auto *activeTab = activeBrowser->GetTab();
		if (!activeBrowser->MaybeGetLatestActiveNavigation() && activeTab->GetId() == tabId
			&& path == activeBrowser->GetDirectoryPath() && generation == self->m_driveGeneration)
		{
			// Cache failures too; a navigation or tab switch can retry.
			self->m_cachedDrivePath = path;
			self->m_cachedDriveText = hasSpace && totalBytes.QuadPart != 0
				? std::format(L"{} {} ({:.0Lf}%)", FormatSizeString(freeBytes.QuadPart),
					self->m_resourceLoader->LoadString(IDS_GENERAL_FREE),
					freeBytes.QuadPart * 100.0 / totalBytes.QuadPart)
				: L"";
			self->m_view->SetPartText(2, self->m_cachedDriveText);
		}
		else if (!activeBrowser->MaybeGetLatestActiveNavigation())
		{
			self->UpdateDriveFreeSpace(*activeTab);
		}
	}
}

void StatusBar::OnWindowDestroyed()
{
	delete this;
}
