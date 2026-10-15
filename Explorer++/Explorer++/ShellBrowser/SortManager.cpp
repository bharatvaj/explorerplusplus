// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "ShellBrowserImpl.h"
#include "ColumnDataRetrieval.h"
#include "Config.h"
#include "ItemData.h"
#include "SortHelper.h"
#include "SortModes.h"
#include "ViewModes.h"
#include <Shlwapi.h>
#include <gdiplus.h>
#include <propkey.h>
#include <atomic>
#include <cassert>

void ShellBrowserImpl::SortFolder()
{
	if (m_folderSettings.sortMode == +SortMode::DateTaken)
	{
		for (const auto &[internalIndex, itemInfo] : m_itemInfoMap)
		{
			if (!m_dateTakenSortKeys.contains(internalIndex)
				&& !m_pendingDateTakenSortItems.contains(internalIndex))
			{
				QueueDateTakenSortItem(internalIndex);
			}
		}
	}

	SendMessage(m_listView, LVM_SORTITEMS, reinterpret_cast<WPARAM>(this),
		reinterpret_cast<LPARAM>(SortStub));

	if (m_folderSettings.viewMode == +ViewMode::Details)
	{
		ApplyHeaderSortArrow();
	}
}

void ShellBrowserImpl::QueueDateTakenSortItem(int internalIndex)
{
	if (m_folderSettings.sortMode != +SortMode::DateTaken)
	{
		return;
	}

	m_dateTakenSortKeys.erase(internalIndex);
	// IDs are shared across tabs so a late post to a recycled HWND can't match a new tab's work.
	static std::atomic<int> nextResultId = 0;
	int resultId = ++nextResultId;
	m_pendingDateTakenSortItems[internalIndex] = resultId;
	auto itemInfo = getBasicItemInfo(internalIndex);
	auto result = m_columnThreadPool.push(
		[listView = m_listView, resultId, itemInfo = std::move(itemInfo),
			cancelled = m_dateTakenSortCancelled](int) {
			if (cancelled->load())
			{
				return std::wstring{};
			}
			auto key = GetImageColumnText(itemInfo, PropertyTagDateTime);
			if (!cancelled->load())
			{
				PostMessage(listView, WM_APP_DATE_TAKEN_SORT_READY, resultId, 0);
			}
			return key;
		});
	m_dateTakenSortResults.emplace(resultId,
		std::make_pair(internalIndex, std::move(result)));
}

void ShellBrowserImpl::ProcessDateTakenSortResult(int resultId)
{
	auto result = m_dateTakenSortResults.find(resultId);
	if (result == m_dateTakenSortResults.end())
	{
		return;
	}

	int internalIndex = result->second.first;
	auto key = result->second.second.get();
	m_dateTakenSortResults.erase(result);

	auto pending = m_pendingDateTakenSortItems.find(internalIndex);
	if (pending == m_pendingDateTakenSortItems.end() || pending->second != resultId)
	{
		return;
	}

	m_pendingDateTakenSortItems.erase(pending);
	m_dateTakenSortKeys.emplace(internalIndex, std::move(key));
	if (m_pendingDateTakenSortItems.empty()
		&& m_folderSettings.sortMode == +SortMode::DateTaken)
	{
		SortFolder();
	}
}

int CALLBACK ShellBrowserImpl::SortStub(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	auto *pShellBrowser = reinterpret_cast<ShellBrowserImpl *>(lParamSort);
	return pShellBrowser->Sort(static_cast<int>(lParam1), static_cast<int>(lParam2));
}

/* Also see NBookmarkHelper::Sort. */
int CALLBACK ShellBrowserImpl::Sort(int InternalIndex1, int InternalIndex2) const
{
	int comparisonResult = 0;

	BasicItemInfo_t basicItemInfo1 = getBasicItemInfo(InternalIndex1);
	BasicItemInfo_t basicItemInfo2 = getBasicItemInfo(InternalIndex2);

	bool isFolder1 = ((basicItemInfo1.wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						 == FILE_ATTRIBUTE_DIRECTORY)
		? true
		: false;
	bool isFolder2 = ((basicItemInfo2.wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						 == FILE_ATTRIBUTE_DIRECTORY)
		? true
		: false;

	/* Folders will by default be sorted separately from files,
	except in the recycle bin. */
	if (!m_config->globalFolderSettings.displayMixedFilesAndFolders && isFolder1 && !isFolder2
		&& !CompareVirtualFolders(CSIDL_BITBUCKET))
	{
		comparisonResult = -1;
	}
	else if (!m_config->globalFolderSettings.displayMixedFilesAndFolders && !isFolder1 && isFolder2
		&& !CompareVirtualFolders(CSIDL_BITBUCKET))
	{
		comparisonResult = 1;
	}
	else
	{
		switch (m_folderSettings.sortMode)
		{
		case SortMode::Name:
			comparisonResult =
				SortByName(basicItemInfo1, basicItemInfo2, m_config->globalFolderSettings);
			break;

		case SortMode::Type:
			comparisonResult = SortByType(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::Size:
			comparisonResult = SortBySize(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::DateModified:
			comparisonResult = SortByDate(basicItemInfo1, basicItemInfo2, DateType::Modified);
			break;

		case SortMode::TotalSize:
			comparisonResult = SortByTotalSize(basicItemInfo1, basicItemInfo2, TRUE);
			break;

		case SortMode::FreeSpace:
			comparisonResult = SortByTotalSize(basicItemInfo1, basicItemInfo2, FALSE);
			break;

		case SortMode::DateDeleted:
			comparisonResult =
				SortByItemDetails(basicItemInfo1, basicItemInfo2, &SCID_DATE_DELETED);
			break;

		case SortMode::OriginalLocation:
			comparisonResult =
				SortByItemDetails(basicItemInfo1, basicItemInfo2, &SCID_ORIGINAL_LOCATION);
			break;

		case SortMode::Attributes:
			comparisonResult = SortByAttributes(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::RealSize:
			comparisonResult = SortByRealSize(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::ShortName:
			comparisonResult = SortByShortName(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::Owner:
			comparisonResult = SortByOwner(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::ProductName:
			comparisonResult =
				SortByVersionInfo(basicItemInfo1, basicItemInfo2, VersionInfoType::ProductName);
			break;

		case SortMode::Company:
			comparisonResult =
				SortByVersionInfo(basicItemInfo1, basicItemInfo2, VersionInfoType::Company);
			break;

		case SortMode::Description:
			comparisonResult =
				SortByVersionInfo(basicItemInfo1, basicItemInfo2, VersionInfoType::Description);
			break;

		case SortMode::FileVersion:
			comparisonResult =
				SortByVersionInfo(basicItemInfo1, basicItemInfo2, VersionInfoType::FileVersion);
			break;

		case SortMode::ProductVersion:
			comparisonResult =
				SortByVersionInfo(basicItemInfo1, basicItemInfo2, VersionInfoType::ProductVersion);
			break;

		case SortMode::ShortcutTo:
			comparisonResult = SortByShortcutTo(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::HardLinks:
			comparisonResult = SortByHardlinks(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::Extension:
			comparisonResult = SortByExtension(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::Created:
			comparisonResult = SortByDate(basicItemInfo1, basicItemInfo2, DateType::Created);
			break;

		case SortMode::Accessed:
			comparisonResult = SortByDate(basicItemInfo1, basicItemInfo2, DateType::Accessed);
			break;

		case SortMode::Title:
			comparisonResult = SortByItemDetails(basicItemInfo1, basicItemInfo2, &PKEY_Title);
			break;

		case SortMode::Subject:
			comparisonResult = SortByItemDetails(basicItemInfo1, basicItemInfo2, &PKEY_Subject);
			break;

		case SortMode::Authors:
			comparisonResult = SortByItemDetails(basicItemInfo1, basicItemInfo2, &PKEY_Author);
			break;

		case SortMode::Keywords:
			comparisonResult = SortByItemDetails(basicItemInfo1, basicItemInfo2, &PKEY_Keywords);
			break;

		case SortMode::Comments:
			comparisonResult = SortByItemDetails(basicItemInfo1, basicItemInfo2, &PKEY_Comment);
			break;

		case SortMode::CameraModel:
			comparisonResult =
				SortByImageProperty(basicItemInfo1, basicItemInfo2, PropertyTagEquipModel);
			break;

		case SortMode::DateTaken:
		{
			const auto key1 = m_dateTakenSortKeys.find(InternalIndex1);
			const auto key2 = m_dateTakenSortKeys.find(InternalIndex2);
			const wchar_t *text1 = key1 == m_dateTakenSortKeys.end() ? L"" : key1->second.c_str();
			const wchar_t *text2 = key2 == m_dateTakenSortKeys.end() ? L"" : key2->second.c_str();
			comparisonResult = StrCmpLogicalW(text1, text2);
			break;
		}

		case SortMode::Width:
			comparisonResult =
				SortByImageProperty(basicItemInfo1, basicItemInfo2, PropertyTagImageWidth);
			break;

		case SortMode::Height:
			comparisonResult =
				SortByImageProperty(basicItemInfo1, basicItemInfo2, PropertyTagImageHeight);
			break;

		case SortMode::VirtualComments:
			comparisonResult = SortByVirtualComments(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::FileSystem:
			comparisonResult = SortByFileSystem(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::NumPrinterDocuments:
			comparisonResult = SortByPrinterProperty(basicItemInfo1, basicItemInfo2,
				PrinterInformationType::NumJobs);
			break;

		case SortMode::PrinterStatus:
			comparisonResult = SortByPrinterProperty(basicItemInfo1, basicItemInfo2,
				PrinterInformationType::Status);
			break;

		case SortMode::PrinterComments:
			comparisonResult = SortByPrinterProperty(basicItemInfo1, basicItemInfo2,
				PrinterInformationType::Comments);
			break;

		case SortMode::PrinterLocation:
			comparisonResult = SortByPrinterProperty(basicItemInfo1, basicItemInfo2,
				PrinterInformationType::Location);
			break;

		case SortMode::NetworkAdapterStatus:
			comparisonResult = SortByNetworkAdapterStatus(basicItemInfo1, basicItemInfo2);
			break;

		case SortMode::MediaBitrate:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Bitrate);
			break;

		case SortMode::MediaCopyright:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Copyright);
			break;

		case SortMode::MediaDuration:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Duration);
			break;

		case SortMode::MediaProtected:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Protected);
			break;

		case SortMode::MediaRating:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Rating);
			break;

		case SortMode::MediaAlbumArtist:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::AlbumArtist);
			break;

		case SortMode::MediaAlbum:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::AlbumTitle);
			break;

		case SortMode::MediaBeatsPerMinute:
			comparisonResult = SortByMediaMetadata(basicItemInfo1, basicItemInfo2,
				MediaMetadataType::BeatsPerMinute);
			break;

		case SortMode::MediaComposer:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Composer);
			break;

		case SortMode::MediaConductor:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Conductor);
			break;

		case SortMode::MediaDirector:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Director);
			break;

		case SortMode::MediaGenre:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Genre);
			break;

		case SortMode::MediaLanguage:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Language);
			break;

		case SortMode::MediaBroadcastDate:
			comparisonResult = SortByMediaMetadata(basicItemInfo1, basicItemInfo2,
				MediaMetadataType::BroadcastDate);
			break;

		case SortMode::MediaChannel:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Channel);
			break;

		case SortMode::MediaStationName:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::StationName);
			break;

		case SortMode::MediaMood:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Mood);
			break;

		case SortMode::MediaParentalRating:
			comparisonResult = SortByMediaMetadata(basicItemInfo1, basicItemInfo2,
				MediaMetadataType::ParentalRating);
			break;

		case SortMode::MediaParentalRatingReason:
			comparisonResult = SortByMediaMetadata(basicItemInfo1, basicItemInfo2,
				MediaMetadataType::ParentalRatingReason);
			break;

		case SortMode::MediaPeriod:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Period);
			break;

		case SortMode::MediaProducer:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Producer);
			break;

		case SortMode::MediaPublisher:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Publisher);
			break;

		case SortMode::MediaWriter:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Writer);
			break;

		case SortMode::MediaYear:
			comparisonResult =
				SortByMediaMetadata(basicItemInfo1, basicItemInfo2, MediaMetadataType::Year);
			break;

		default:
			assert(false);
			break;
		}
	}

	if (comparisonResult == 0)
	{
		/* By default, items that are equal will be sub-sorted
		by their display names. */
		if (m_config->globalFolderSettings.useNaturalSortOrder)
		{
			comparisonResult =
				StrCmpLogicalW(basicItemInfo1.szDisplayName, basicItemInfo2.szDisplayName);
		}
		else
		{
			comparisonResult = StrCmpIW(basicItemInfo1.szDisplayName, basicItemInfo2.szDisplayName);
		}
	}

	if (m_folderSettings.sortDirection == +SortDirection::Descending)
	{
		comparisonResult = -comparisonResult;
	}

	return comparisonResult;
}
