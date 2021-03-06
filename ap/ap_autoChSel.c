/****************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 * (c) Copyright 2002, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ****************************************************************************

    Module Name:

    Abstract:
*/


#include "rt_config.h"
#include "ap_autoChSel.h"


extern u8 ZeroSsid[32];

static inline INT GetABandChOffset(
	IN INT Channel)
{
	if ((Channel == 36) || (Channel == 44)
			|| (Channel == 52) || (Channel == 60)
			|| (Channel == 100) || (Channel == 108)
			|| (Channel == 116) || (Channel == 124)
			|| (Channel == 132) || (Channel == 149)
			|| (Channel == 157))
	{
		return 1;
	}
	else if ((Channel == 40) || (Channel == 48)
			|| (Channel == 56) || (Channel == 64)
			|| (Channel == 104) || (Channel == 112)
			|| (Channel == 120) || (Channel == 128)
			|| (Channel == 136) || (Channel == 153)
			|| (Channel == 161))
	{
		return -1;
	}
	return 0;
}

ULONG AutoChBssSearchWithSSID(
	IN struct rtmp_adapter *pAd,
	IN u8 *Bssid,
	IN u8 *pSsid,
	IN u8 SsidLen,
	IN u8 Channel)
{
	u8 i;
	PBSSINFO pBssInfoTab = pAd->pBssInfoTab;

	if(pBssInfoTab == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("pAd->pBssInfoTab equal NULL.\n"));
		return (ULONG)BSS_NOT_FOUND;
	}

	for (i = 0; i < pBssInfoTab->BssNr; i++)
	{
		if ((((pBssInfoTab->BssEntry[i].Channel <= 14) && (Channel <= 14)) ||
			((pBssInfoTab->BssEntry[i].Channel > 14) && (Channel > 14))) &&
			MAC_ADDR_EQUAL(&(pBssInfoTab->BssEntry[i].Bssid), Bssid) &&
			(SSID_EQUAL(pSsid, SsidLen, pBssInfoTab->BssEntry[i].Ssid, pBssInfoTab->BssEntry[i].SsidLen) ||
			(NdisEqualMemory(pSsid, ZeroSsid, SsidLen)) ||
			(NdisEqualMemory(pBssInfoTab->BssEntry[i].Ssid, ZeroSsid, pBssInfoTab->BssEntry[i].SsidLen))))
		{
			return i;
		}
	}
	return (ULONG)BSS_NOT_FOUND;
}

static inline VOID AutoChBssEntrySet(
	OUT BSSENTRY *pBss,
	IN u8 *pBssid,
	IN CHAR Ssid[],
	IN u8 SsidLen,
	IN u8 Channel,
	IN u8 ExtChOffset,
	IN CHAR Rssi)
{
	COPY_MAC_ADDR(pBss->Bssid, pBssid);
	if (SsidLen > 0)
	{
		/*
			For hidden SSID AP, it might send beacon with SSID len equal to 0,
			Or send beacon /probe response with SSID len matching real SSID length,
			but SSID is all zero. such as "00-00-00-00" with length 4.
			We have to prevent this case overwrite correct table
		*/
		if (NdisEqualMemory(Ssid, ZeroSsid, SsidLen) == 0)
		{
			memmove(pBss->Ssid, Ssid, SsidLen);
			pBss->SsidLen = SsidLen;
		}
	}

	pBss->Channel = Channel;
	pBss->ExtChOffset = ExtChOffset;
	pBss->Rssi = Rssi;

	return;
}

static inline VOID AutoChBssTableReset(
	IN struct rtmp_adapter *pAd)
{
	if (pAd->pBssInfoTab)
		memset(pAd->pBssInfoTab, 0, sizeof(BSSINFO));
	else
		DBGPRINT(RT_DEBUG_ERROR, ("pAd->pBssInfoTab equal NULL.\n"));

	return;
}

static VOID ChannelInfoReset(
	IN struct rtmp_adapter *pAd)
{
	if (pAd->pChannelInfo)
		memset(pAd->pChannelInfo, 0, sizeof(CHANNELINFO));
	else
		DBGPRINT(RT_DEBUG_ERROR, ("pAd->pChannelInfo equal NULL.\n"));

	return;
}

VOID UpdateChannelInfo(
	IN struct rtmp_adapter *pAd,
	IN int ch_index,
	IN ChannelSel_Alg Alg)
{
	if(pAd->pChannelInfo != NULL)
	{
		uint32_t BusyTime;

		if ( Alg == ChannelAlgCCA )
		{
			RX_STA_CNT1_STRUC RxStaCnt1;
			RxStaCnt1.word = mt76u_reg_read(pAd, RX_STA_CNT1);
			pAd->RalinkCounters.OneSecFalseCCACnt += RxStaCnt1.field.FalseCca;
			pAd->pChannelInfo->FalseCCA[ch_index] = RxStaCnt1.field.FalseCca;
		}


		/*
			do busy time statistics for primary channel
			scan time 400ms, beacon interval 100 ms
		*/

		BusyTime = mt76u_reg_read(pAd, CH_BUSY_STA);
#ifdef AP_QLOAD_SUPPORT
		pAd->pChannelInfo->chanbusytime[ch_index] = (BusyTime * 100) / AUTO_CHANNEL_SEL_TIMEOUT;
#endif /* AP_QLOAD_SUPPORT */
	}
	else
		DBGPRINT(RT_DEBUG_ERROR, ("pAd->pChannelInfo equal NULL.\n"));

	return;
}

static inline INT GetChIdx(
	IN struct rtmp_adapter *pAd,
	IN u8 Channel)
{
	INT Idx;

	Idx = -1;
	for (Idx = 0; Idx < pAd->ChannelListNum; Idx++)
	{
		if (Channel == pAd->ChannelList[Idx].Channel)
			break;
	}

	return Idx;
}

static inline VOID AutoChannelSkipListSetDirty(
	IN struct rtmp_adapter *pAd)
{
	u8 i;
	for (i=0; i < pAd->ApCfg.AutoChannelSkipListNum ; i++)
	{
			u8 channel_idx = GetChIdx(pAd, pAd->ApCfg.AutoChannelSkipList[i]);
			if ( channel_idx != pAd->ChannelListNum )
			{
				pAd->pChannelInfo->SkipList[channel_idx] = true;
			}
	}
}

static inline bool AutoChannelSkipListCheck(
	IN struct rtmp_adapter *pAd,
	IN u8 		Ch)
{
	u8 i;
	bool result = false;

	for (i=0; i < pAd->ApCfg.AutoChannelSkipListNum ; i++)
	{
		if (Ch == pAd->ApCfg.AutoChannelSkipList[i])
		{
			result = true;
			break;
		}
	}
	return result;
}

static inline bool BW40_ChannelCheck(
	IN u8 ch)
{
	INT i;
	bool result = true;
	u8 NorBW40_CH[] = {140, 165};
	u8 NorBW40ChNum = sizeof(NorBW40_CH) / sizeof(u8);

	for (i=0; i<NorBW40ChNum; i++)
	{
		if (ch == NorBW40_CH[i])
		{
			result = false;
			break;
		}
	}

	return result;
}

static inline u8 SelectClearChannelRandom(
	IN struct rtmp_adapter *pAd
	)
{
	u8 cnt, ch = 0, i, RadomIdx;
	/*bool bFindIt = false;*/
	UINT8 TempChList[MAX_NUM_OF_CHANNELS] = {0};

	if (pAd->CommonCfg.bIEEE80211H)
	{
		cnt = 0;

		/* Filter out an available channel list */
		for (i = 0; i < pAd->ChannelListNum; i++)
		{
			/* Check DFS channel RemainingTimeForUse */
			if (pAd->ChannelList[i].RemainingTimeForUse)
				continue;

			/* Check skip channel list */
			if (AutoChannelSkipListCheck(pAd, pAd->ChannelList[i].Channel) == true)
				continue;

			/* Check N-group of BW40 */
			if ((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40 &&
				(pAd->CommonCfg.vht_bw == VHT_BW_2040)) &&
				!(pAd->ChannelList[i].Flags & CHANNEL_40M_CAP))
				continue;

			/* Check VHT-group of BW80 */
			if (pAd->CommonCfg.vht_bw == VHT_BW_80 &&
				!(pAd->ChannelList[i].Flags & CHANNEL_80M_CAP))
				continue;

			/* Store available channel to temp list */
			TempChList[cnt++] = pAd->ChannelList[i].Channel;
		}

		/* Randomly select a channel from temp list */
		if (cnt)
		{
			RadomIdx = RandomByte2(pAd)%cnt;
			ch = TempChList[RadomIdx];
		}
		else
		{
			ch = get_channel_by_reference(pAd, 1);
		}

	}
	else
	{
		ch = pAd->ChannelList[RandomByte2(pAd)%pAd->ChannelListNum].Channel;
		if (ch == 0)
			ch = FirstChannel(pAd);
	}
	DBGPRINT(RT_DEBUG_TRACE,("%s(): Select Channel %d\n", __FUNCTION__, ch));
	return ch;

}

/*
	==========================================================================
	Description:
        This routine calaulates the dirtyness of all channels by the
        CCA value  and Rssi. Store dirtyness to pChannelInfo strcut.
		This routine is called at iwpriv cmmand or initialization. It chooses and returns
		a good channel whith less interference.
	Return:
		ch -  channel number that
	NOTE:
	==========================================================================
 */
static inline u8 SelectClearChannelCCA(
	IN struct rtmp_adapter *pAd
	)
{
	#define CCA_THRESHOLD (100)

	PBSSINFO pBssInfoTab = pAd->pBssInfoTab;
	PCHANNELINFO pChannelInfo = pAd->pChannelInfo;
	INT ch = 1, channel_idx, BssTab_idx;
	BSSENTRY *pBss;
	uint32_t min_dirty, min_falsecca;
	int candidate_ch;
	u8  ExChannel[2] = {0}, candidate_ExChannel[2] = {0};
	u8 base;

	if(pBssInfoTab == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("pAd->pBssInfoTab equal NULL.\n"));
		return (FirstChannel(pAd));
	}

	if(pChannelInfo == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("pAd->pChannelInfo equal NULL.\n"));
		return (FirstChannel(pAd));
	}

	for (BssTab_idx = 0; BssTab_idx < pBssInfoTab->BssNr; BssTab_idx++)
	{
		pBss = &(pBssInfoTab->BssEntry[BssTab_idx]);
		channel_idx = GetChIdx(pAd, pBss->Channel);
		if (channel_idx < 0 )
			continue;


		if (pBss->Rssi >= RSSI_TO_DBM_OFFSET-50)
		{
			/* high signal >= -50 dbm */
			pChannelInfo->dirtyness[channel_idx] += 50;
		}
		else if (pBss->Rssi <= RSSI_TO_DBM_OFFSET-80)
		{
			/* low signal <= -80 dbm */
			pChannelInfo->dirtyness[channel_idx] += 30;
		}
		else
		{
			/* mid signal -50 ~ -80 dbm */
			pChannelInfo->dirtyness[channel_idx] += 40;
		}

		pChannelInfo->dirtyness[channel_idx] += 40;

		{
			INT BelowBound;
			INT AboveBound;
			INT loop;

			switch(pBss->ExtChOffset)
			{
				case EXTCHA_ABOVE:
					BelowBound = pChannelInfo->IsABand ? 1 : 4;
					AboveBound = pChannelInfo->IsABand ? 2 : 8;
					break;

				case EXTCHA_BELOW:
					BelowBound = pChannelInfo->IsABand ? 2 : 8;
					AboveBound = pChannelInfo->IsABand ? 1 : 4;
					break;

				default:
					BelowBound = pChannelInfo->IsABand ? 1 : 4;
					AboveBound = pChannelInfo->IsABand ? 1 : 4;
					break;
			}

			/* check neighbor channel */
			for (loop = (channel_idx+1); loop <= (channel_idx+AboveBound); loop++)
			{
				if (loop >= MAX_NUM_OF_CHANNELS)
					break;

				if (pAd->ChannelList[loop].Channel - pAd->ChannelList[loop-1].Channel > 4)
					break;

				pChannelInfo->dirtyness[loop] += ((9 - (loop - channel_idx)) * 4);
			}
            /* check neighbor channel */
			for (loop=(channel_idx-1); loop >= (channel_idx-BelowBound); loop--)
			{
				if (loop < 0)
					break;

				if (pAd->ChannelList[loop+1].Channel - pAd->ChannelList[loop].Channel > 4)
					continue;

				pChannelInfo->dirtyness[loop] +=
					((9 - (channel_idx - loop)) * 4);
			}
		}

		DBGPRINT(RT_DEBUG_TRACE,(" ch%d bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
			pBss->Channel, pBss->Bssid[0], pBss->Bssid[1], pBss->Bssid[2], pBss->Bssid[3], pBss->Bssid[4], pBss->Bssid[5]));
	}

	AutoChannelSkipListSetDirty(pAd);

	DBGPRINT(RT_DEBUG_TRACE, ("=====================================================\n"));
	for (channel_idx = 0; channel_idx < pAd->ChannelListNum; channel_idx++)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("Channel %d : Dirty = %ld, False CCA = %u, Busy Time = %u, Skip Channel = %s\n",
					pAd->ChannelList[channel_idx].Channel,
					pChannelInfo->dirtyness[channel_idx],
					pChannelInfo->FalseCCA[channel_idx],
#ifdef AP_QLOAD_SUPPORT
					pChannelInfo->chanbusytime[channel_idx],
#else
					0,
#endif /* AP_QLOAD_SUPPORT */
					(pChannelInfo->SkipList[channel_idx] == true) ? "true" : "false"));
	}
	DBGPRINT(RT_DEBUG_TRACE, ("=====================================================\n"));

	min_dirty = min_falsecca = 0xFFFFFFFF;

	/*
	 * Rule 1. Pick up a good channel that False_CCA =< CCA_THRESHOLD
	 *		   by dirtyness
	 */
	candidate_ch = -1;

	for (channel_idx = 0; channel_idx < pAd->ChannelListNum; channel_idx++)
	{
		if (pChannelInfo->SkipList[channel_idx] == true)
			continue;

		if (pChannelInfo->FalseCCA[channel_idx] <= CCA_THRESHOLD)
		{
			uint32_t dirtyness = pChannelInfo->dirtyness[channel_idx];
			ch = pAd->ChannelList[channel_idx].Channel;

#ifdef AP_QLOAD_SUPPORT
			/* QLOAD ALARM */
			/* when busy time of a channel > threshold, skip it */
			/* TODO: Use weight for different references to do channel selection */
			if (QBSS_LoadIsBusyTimeAccepted(pAd,
				pChannelInfo->chanbusytime[channel_idx]) == false)
			{
				/* check next one */
				continue;
			}
#endif /* AP_QLOAD_SUPPORT */

			/*
				User require 40MHz Bandwidth.
				In the case, ignor all channel
				doesn't support 40MHz Bandwidth.
			*/
			if (((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) &&
				(pAd->CommonCfg.vht_bw == VHT_BW_2040))
				&& (pChannelInfo->IsABand && (GetABandChOffset(ch) == 0))) {
				continue;
			}
			else if (pAd->CommonCfg.vht_bw == VHT_BW_80) {
				if (!AC_ChannelGroupCheck(pAd, ch)) {
					/* not in the BW_80 channel group */
					continue;
				}
			}

			/*
				Need to Consider the dirtyness of extending channel
				in 40 MHz bandwidth channel.
			*/
			if (pAd->pChannelInfo->IsABand)
			{
				/* 5G Hz */
				if ((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) &&
				(pAd->CommonCfg.vht_bw == VHT_BW_2040))
				{
					if (((channel_idx + GetABandChOffset(ch)) >=0)
							&& ((channel_idx + GetABandChOffset(ch)) < pAd->ChannelListNum))
					{
						INT ChOffsetIdx = channel_idx + GetABandChOffset(ch);
						dirtyness += pChannelInfo->dirtyness[ChOffsetIdx];
					}
				}
				else if (pAd->CommonCfg.vht_bw == VHT_BW_80) {
					/* the same dirtyness with neighbor */
					INT	vht_ch_idx = get_vht_neighbor_index(ch);

					dirtyness = pChannelInfo->dirtyness[vht_ch_idx+channel_idx] +
						pChannelInfo->dirtyness[vht_ch_idx+(channel_idx+1)] +
						pChannelInfo->dirtyness[vht_ch_idx+(channel_idx+2)] +
						pChannelInfo->dirtyness[vht_ch_idx+(channel_idx+3)];
				}
			}
			else
			{ /* 2.4G Hz */
				u8 ExChannel_idx = 0;
				if (pAd->ChannelList[channel_idx].Channel == 14)
				{
					dirtyness = 0xFFFFFFFF;
					break;
				}
				else
				{
					u8 ExChannel_idx = 0;
					if (pAd->ChannelList[channel_idx].Channel == 14)
					{
						dirtyness = 0xFFFFFFFF;
						break;
					}
					else
					{
						memset(ExChannel, 0, sizeof(ExChannel));
						if (((channel_idx - 4) >=0) && ((channel_idx - 4) < pAd->ChannelListNum))
						{
							dirtyness += pChannelInfo->dirtyness[channel_idx - 4];
							ExChannel[ExChannel_idx++] = pAd->ChannelList[channel_idx - 4].Channel;
					    }

						if (((channel_idx + 4) >=0) && ((channel_idx + 4) < pAd->ChannelListNum))
						{
						    dirtyness += pChannelInfo->dirtyness[channel_idx + 4];
						    ExChannel[ExChannel_idx++] = pAd->ChannelList[channel_idx + 4].Channel;
						}
					}
				}
			}

			if ((min_dirty > dirtyness))
			{
				min_dirty = dirtyness;
				candidate_ch = channel_idx;
				memmove(candidate_ExChannel, ExChannel, 2);
			}
		}
	}

	if (candidate_ch >= 0)
	{
		ch = pAd->ChannelList[candidate_ch].Channel;
		DBGPRINT(RT_DEBUG_TRACE, ("Rule 1 CCA value : Min Dirtiness (Include extension channel) ==> Select Channel %d \n", ch));
		DBGPRINT(RT_DEBUG_TRACE, ("Min Dirty = %u\n", min_dirty));
		DBGPRINT(RT_DEBUG_TRACE, ("ExChannel = %d , %d\n", candidate_ExChannel[0], candidate_ExChannel[1]));
		if (pAd->CommonCfg.RegTransmitSetting.field.BW == BW_20)
			DBGPRINT(RT_DEBUG_TRACE, ("BW        = %s\n", "20"));
		else if (pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40)
			DBGPRINT(RT_DEBUG_TRACE, ("BW        = %s\n", "40"));
		else
			DBGPRINT(RT_DEBUG_TRACE, ("BW        = %s\n", "80"));
		return ch;
	}

	/*
	 * Rule 2. Pick up a good channel that False_CCA > CCA_THRESHOLD
	 *		   by FalseCCA (FalseCCA + Dirtyness)
	 */
	candidate_ch = -1;
	for (channel_idx = 0; channel_idx < pAd->ChannelListNum; channel_idx++)
	{
		if (pChannelInfo->SkipList[channel_idx] == true)
			continue;

		if (pChannelInfo->FalseCCA[channel_idx] > CCA_THRESHOLD)
		{
			uint32_t falsecca = pChannelInfo->FalseCCA[channel_idx] + pChannelInfo->dirtyness[channel_idx];
			ch = pAd->ChannelList[channel_idx].Channel;

			if (((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) &&
				(pAd->CommonCfg.vht_bw == VHT_BW_2040))
				&& (pChannelInfo->IsABand && (GetABandChOffset(ch) == 0))) {
				continue;
			}
			else if (pAd->CommonCfg.vht_bw == VHT_BW_80) {
				if (!AC_ChannelGroupCheck(pAd, ch)) {
					/* not in the BW_80 channel group */
					continue;
				}
			}

			if ((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) &&
				(pAd->CommonCfg.vht_bw == VHT_BW_2040))
			{
				if ((GetABandChOffset(ch) != 0)
						&& ((channel_idx + GetABandChOffset(ch)) >=0)
						&& ((channel_idx + GetABandChOffset(ch)) < pAd->ChannelListNum))
				{
					INT ChOffsetIdx = channel_idx + GetABandChOffset(ch);
					falsecca += (pChannelInfo->FalseCCA[ChOffsetIdx] +
							pChannelInfo->dirtyness[ChOffsetIdx]);
				}
			}
			else if (pAd->CommonCfg.vht_bw == VHT_BW_80) {
				/* the same falsecca with neighbor */
				INT	vht_ch_idx = get_vht_neighbor_index(ch);

				falsecca = (pChannelInfo->dirtyness[vht_ch_idx+channel_idx] +
					pChannelInfo->FalseCCA[vht_ch_idx+channel_idx]) +
					(pChannelInfo->dirtyness[vht_ch_idx+(channel_idx+1)] +
					pChannelInfo->FalseCCA[vht_ch_idx+(channel_idx+1)]) +
					(pChannelInfo->dirtyness[vht_ch_idx+(channel_idx+2)] +
					pChannelInfo->FalseCCA[vht_ch_idx+(channel_idx+2)]) +
					(pChannelInfo->dirtyness[vht_ch_idx+(channel_idx+3)] +
					pChannelInfo->FalseCCA[vht_ch_idx+(channel_idx+3)]);
			}

#ifdef AP_QLOAD_SUPPORT
			/* QLOAD ALARM */
			/* when busy time of a channel > threshold, skip it */
			/* TODO: Use weight for different references to do channel selection */
			if (QBSS_LoadIsBusyTimeAccepted(pAd,
				pChannelInfo->chanbusytime[channel_idx]) == false)
			{
				/* check next one */
				continue;
			}
#endif /* AP_QLOAD_SUPPORT */

			if ((min_falsecca > falsecca))
			{
				min_falsecca = falsecca;
				candidate_ch = channel_idx;
			}
		}
	}

	if (candidate_ch >= 0)
	{
		ch = pAd->ChannelList[candidate_ch].Channel;
		DBGPRINT(RT_DEBUG_TRACE, ("Rule 2 CCA value : Min False CCA value ==> Select Channel %d, min falsecca = %d \n", ch, min_falsecca));
		return	ch;
	}

	base = RandomByte2(pAd);
	for (channel_idx=0 ; channel_idx < pAd->ChannelListNum ; channel_idx++)
	{
		ch = pAd->ChannelList[(base + channel_idx) % pAd->ChannelListNum].Channel;

		if (AutoChannelSkipListCheck(pAd, ch))
			continue;

		if ((pAd->ApCfg.bAvoidDfsChannel == true)
			&& (pChannelInfo->IsABand == true)
			&& RadarChannelCheck(pAd, ch))
			continue;

		break;
	}
	DBGPRINT(RT_DEBUG_TRACE, ("Rule 3 CCA value : Randomly Select ==> Select Channel %d\n", ch));
	return ch;
}

/*
	==========================================================================
	Description:
        This routine calaulates the dirtyness of all channels by the dirtiness value and
        number of AP in each channel and stores in pChannelInfo strcut.
		This routine is called at iwpriv cmmand or initialization. It chooses and returns
		a good channel whith less interference.
	Return:
		ch -  channel number that
	NOTE:
	==========================================================================
 */

static inline u8 SelectClearChannelApCnt(
	IN struct rtmp_adapter *pAd
	)
{
    /*PBSSINFO pBssInfoTab = pAd->pBssInfoTab; */
	PCHANNELINFO pChannelInfo = pAd->pChannelInfo;
	/*BSSENTRY *pBss; */
	u8 channel_index = 0,dirty,base = 0;
	u8 final_channel = 0;


	if(pChannelInfo == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("pAd->pChannelInfo equal NULL.\n"));
		return (FirstChannel(pAd));
	}

	/* Calculate Dirtiness */

	for (channel_index=0 ; channel_index < pAd->ChannelListNum ; channel_index++)
	{
		if (pChannelInfo->ApCnt[channel_index] > 0)
	    {
		    INT ll;
		    pChannelInfo->dirtyness[channel_index] += 30;

            /*5G */
		    if (pChannelInfo->IsABand)
		    {
			    int Channel = pAd->ChannelList[channel_index].Channel;

			    /*Make secondary channel dirty */
			    if((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) &&
				(pAd->CommonCfg.vht_bw == VHT_BW_2040))
			    {
					if (Channel > 14)
					{
						if ((Channel == 36) || (Channel == 44)
								|| (Channel == 52) || (Channel== 60)
								|| (Channel == 100) || (Channel == 108)
								|| (Channel == 116) || (Channel == 124)
								|| (Channel == 132) || (Channel == 149)
								|| (Channel == 157))
						{
							if (channel_index + 1 < MAX_NUM_OF_CHANNELS)
								if(pAd->ChannelList[channel_index+1].Channel - pAd->ChannelList[channel_index].Channel == 4)
									pChannelInfo->dirtyness[channel_index+1] += 1;
						}
						else if ((Channel == 40) || (Channel == 48)
								|| (Channel == 56) || (Channel == 64)
								|| (Channel == 104) || (Channel == 112)
								|| (Channel == 120) || (Channel == 128)
								|| (Channel == 136) || (Channel== 153)
								|| (Channel == 161))
						{
							if(channel_index - 1 >= 0)
								if(pAd->ChannelList[channel_index].Channel - pAd->ChannelList[channel_index-1].Channel == 4)
									pChannelInfo->dirtyness[channel_index-1] += 1;
						}
					}
				}
				else if(pAd->CommonCfg.vht_bw == VHT_BW_80) {
					INT	vht_ch_idx = get_vht_neighbor_index(pAd->ChannelList[channel_index].Channel);
					pChannelInfo->dirtyness[vht_ch_idx+channel_index]++;
					pChannelInfo->dirtyness[vht_ch_idx+(channel_index+1)]++;
					pChannelInfo->dirtyness[vht_ch_idx+(channel_index+2)]++;
					pChannelInfo->dirtyness[vht_ch_idx+(channel_index+3)]++;
				}
			}
			/*2.4G */
			if (!pChannelInfo->IsABand)
			{
				int ChanOffset = 0;

				if((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40)&&
				(pAd->CommonCfg.RegTransmitSetting.field.EXTCHA == EXTCHA_BELOW))
				{
				/*
					BW is 40Mhz
					the distance between two channel to prevent interference
					is 4 channel width plus 4 channel width (secondary channel)
				*/
					ChanOffset = 8;
				}
				else
				{
				/*
					BW is 20Mhz
					The channel width of 2.4G band is 5Mhz.
					The distance between two channel to prevent interference is 4 channel width
				*/
					ChanOffset = 4;
				}

				for (ll = channel_index + 1; ll < (channel_index + ChanOffset + 1); ll++)
				{
					if (ll < MAX_NUM_OF_CHANNELS)
						pChannelInfo->dirtyness[ll]++;
				}

				if((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40)&&
					(pAd->CommonCfg.RegTransmitSetting.field.EXTCHA == EXTCHA_ABOVE))
				{
					/* BW is 40Mhz */
					ChanOffset = 8;
				}
				else
				{
					/* BW is 20Mhz */
					ChanOffset = 4;
				}

				for (ll = channel_index - 1; ll > (channel_index - ChanOffset - 1); ll--)
				{
					if (ll >= 0)
						pChannelInfo->dirtyness[ll]++;
				}
			}
    	}
   }/* Calculate Dirtiness */

	AutoChannelSkipListSetDirty(pAd);

   DBGPRINT(RT_DEBUG_TRACE, ("=====================================================\n"));
   for (channel_index=0 ; channel_index < pAd->ChannelListNum ; channel_index++)
   /* debug messages */
		DBGPRINT(RT_DEBUG_TRACE, ("Channel %d : Dirty = %ld, ApCnt=%ld, Busy Time = %d, Skip Channel = %s\n",
				pAd->ChannelList[channel_index].Channel,
				pChannelInfo->dirtyness[channel_index],
				pChannelInfo->ApCnt[channel_index],
#ifdef AP_QLOAD_SUPPORT
				pChannelInfo->chanbusytime[channel_index],
#else
				0,
#endif /* AP_QLOAD_SUPPORT */
				(pChannelInfo->SkipList[channel_index] == true) ? "true" : "false"));
   DBGPRINT(RT_DEBUG_TRACE, ("=====================================================\n"));

   pAd->ApCfg.AutoChannel_Channel = 0;

	/* RULE 1. pick up a good channel that no one used */

	for (channel_index=0 ; channel_index < pAd->ChannelListNum ; channel_index++)
	{
		if (pChannelInfo->SkipList[channel_index] == true)
			continue;

	     if ((pAd->ApCfg.bAvoidDfsChannel == true)
				&&(pChannelInfo->IsABand == true)
				&& RadarChannelCheck(pAd, pAd->ChannelList[channel_index].Channel))
			continue;

#ifdef AP_QLOAD_SUPPORT
		/* QLOAD ALARM */
		if (QBSS_LoadIsBusyTimeAccepted(pAd,
			pChannelInfo->chanbusytime[channel_index]) == false)
			continue;
#endif /* AP_QLOAD_SUPPORT */

		/* Check BW40/80 channel group */
		if (pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) {
			if (!N_ChannelGroupCheck(pAd, pAd->ChannelList[channel_index].Channel)) {
				printk ("channel_%d is not in BW_40 channel group\n",
						pAd->ChannelList[channel_index].Channel);
				continue;
			}
		}
		else if (pAd->CommonCfg.vht_bw == VHT_BW_80) {
			if (!AC_ChannelGroupCheck(pAd, pAd->ChannelList[channel_index].Channel)) {
				printk ("channel_%d is not in BW_80 channel group\n",
						pAd->ChannelList[channel_index].Channel);
				continue;
			}
		}

		if (pChannelInfo->dirtyness[channel_index] == 0)
			break;
	}
	if (channel_index < pAd->ChannelListNum)
	{
		DBGPRINT(RT_DEBUG_TRACE,("Rule 1 APCnt : dirtiness == 0 (no one used and no interference) ==> Select Channel %d\n", pAd->ChannelList[channel_index].Channel));

		return pAd->ChannelList[channel_index].Channel;
	}

	/* RULE 2. if not available, then co-use a channel that's no interference (dirtyness=30) */
	/* RULE 3. if not available, then co-use a channel that has minimum interference (dirtyness=31,32) */
	for (dirty = 30; dirty <= 32; dirty++)
	{
		bool candidate[MAX_NUM_OF_CHANNELS+1], candidate_num=0;
		u8 min_ApCnt = 255;
		final_channel = 0;

		memset(candidate, 0, MAX_NUM_OF_CHANNELS+1);
		for (channel_index=0 ; channel_index < pAd->ChannelListNum ; channel_index++)
		{
			if (pChannelInfo->SkipList[channel_index] == true)
				continue;

			if (pChannelInfo->dirtyness[channel_index] == dirty)
			{
				candidate[channel_index]=true;
				candidate_num = true;
			}
		}
		/* if there's more than 1 candidate, pick up the channel with minimum RSSI */
		if (candidate_num)
		{
			for (channel_index=0 ; channel_index < pAd->ChannelListNum ; channel_index++)
			{

#ifdef AP_QLOAD_SUPPORT
				/* QLOAD ALARM */
				/* when busy time of a channel > threshold, skip it */
				/* TODO: Use weight for different references to do channel selection */
				if (QBSS_LoadIsBusyTimeAccepted(pAd,
					pChannelInfo->chanbusytime[channel_index]) == false)
				{
					/* check next one */
					continue;
				}
#endif /* AP_QLOAD_SUPPORT */

				if (candidate[channel_index] && (pChannelInfo->ApCnt[channel_index] < min_ApCnt))
				{

					if(((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) &&
						(pAd->CommonCfg.vht_bw == VHT_BW_2040))
						&& (BW40_ChannelCheck(pAd->ChannelList[channel_index].Channel) == false)) {
						continue;
					}
					else if (pAd->CommonCfg.vht_bw == VHT_BW_80) {
						if (!AC_ChannelGroupCheck(pAd, pAd->ChannelList[channel_index].Channel)) {
							/* not in the BW_80 channel group */
							continue;
						}
					}

					if ((pAd->ApCfg.bAvoidDfsChannel == true)
							&&(pChannelInfo->IsABand == true)
							&& RadarChannelCheck(pAd, pAd->ChannelList[channel_index].Channel))
						continue;

					final_channel = pAd->ChannelList[channel_index].Channel;
					min_ApCnt = pChannelInfo->ApCnt[channel_index];
				}
			}
			if (final_channel != 0)
			{
				DBGPRINT(RT_DEBUG_TRACE,("Rule 2 APCnt : minimum APCnt with  minimum interference(dirtiness: 30~32) ==> Select Channel %d\n", final_channel));
				DBGPRINT(RT_DEBUG_TRACE,(" Dirtiness = %d ,  Min ApCnt = %d\n", dirty, min_ApCnt));
				return final_channel;
			}
		}
	}
	/* RULE 3. still not available, pick up the random channel */
	base = RandomByte2(pAd);

	for (channel_index=0 ; channel_index < pAd->ChannelListNum ; channel_index++)
	{
		final_channel = pAd->ChannelList[(base + channel_index) % pAd->ChannelListNum].Channel;

		if (AutoChannelSkipListCheck(pAd, final_channel))
			continue;

		if ((pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) && (pAd->CommonCfg.vht_bw == VHT_BW_2040))
		{
			if (!N_ChannelGroupCheck(pAd, pAd->ChannelList[channel_index].Channel))
			{
				/* not in the BW_40 channel group */
				continue;
			}
		}
		else if (pAd->CommonCfg.vht_bw == VHT_BW_80) {
			if (!AC_ChannelGroupCheck(pAd, pAd->ChannelList[channel_index].Channel)) {
				/* not in the BW_80 channel group */
				continue;
			}
		}

		if ((pAd->ApCfg.bAvoidDfsChannel == true)
			&&(pChannelInfo->IsABand == true)
			&& RadarChannelCheck(pAd, final_channel))
				continue;

		break;
	}
	DBGPRINT(RT_DEBUG_TRACE,("Rule 3 APCnt : Randomly Select  ==> Select Channel %d\n",final_channel));
	return final_channel;

}

ULONG AutoChBssInsertEntry(
	IN struct rtmp_adapter *pAd,
	IN u8 *pBssid,
	IN CHAR Ssid[],
	IN u8 SsidLen,
	IN u8 ChannelNo,
	IN u8 ExtChOffset,
	IN CHAR Rssi)
{
	ULONG	Idx;
	PBSSINFO pBssInfoTab = pAd->pBssInfoTab;

	if(pBssInfoTab == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("pAd->pBssInfoTab equal NULL.\n"));
		return BSS_NOT_FOUND;
	}

	Idx = AutoChBssSearchWithSSID(pAd, pBssid, (u8 *)Ssid, SsidLen, ChannelNo);
	if (Idx == BSS_NOT_FOUND)
	{
		if (pBssInfoTab->BssNr >= MAX_LEN_OF_BSS_TABLE)
			return BSS_NOT_FOUND;
		Idx = pBssInfoTab->BssNr;
		AutoChBssEntrySet(&pBssInfoTab->BssEntry[Idx], pBssid, Ssid, SsidLen,
							ChannelNo, ExtChOffset, Rssi);
		pBssInfoTab->BssNr++;
	}
	else
	{
		AutoChBssEntrySet(&pBssInfoTab->BssEntry[Idx], pBssid, Ssid, SsidLen,
							ChannelNo, ExtChOffset, Rssi);
	}

	return Idx;
}


void AutoChBssTableInit(
	IN struct rtmp_adapter *pAd)
{
/*	pAd->pBssInfoTab = (PBSSINFO)kmalloc(sizeof(BSSINFO), GFP_ATOMIC); */
	pAd->pBssInfoTab = kmalloc(sizeof(BSSINFO), GFP_ATOMIC);
	if (pAd->pBssInfoTab)
		memset(pAd->pBssInfoTab, 0, sizeof(BSSINFO));
	else
		DBGPRINT(RT_DEBUG_ERROR, ("%s Fail to alloc memory for pAd->pBssInfoTab", __FUNCTION__));

	return;
}

void ChannelInfoInit(
	IN struct rtmp_adapter *pAd)
{
/*	pAd->pChannelInfo = (PCHANNELINFO)kmalloc(sizeof(CHANNELINFO), GFP_ATOMIC); */
	pAd->pChannelInfo = kmalloc(sizeof(CHANNELINFO), GFP_ATOMIC);
	if (pAd->pChannelInfo)
		memset(pAd->pChannelInfo, 0, sizeof(CHANNELINFO));
	else
		DBGPRINT(RT_DEBUG_ERROR, ("%s Fail to alloc memory for pAd->pChannelInfo", __FUNCTION__));


	return;
}

void AutoChBssTableDestroy(
	IN struct rtmp_adapter *pAd)
{
	if (pAd->pBssInfoTab)
	{
/*		kfree(pAd->pBssInfoTab); */
		kfree(pAd->pBssInfoTab);
		pAd->pBssInfoTab = NULL;
	}

	return;
}

void ChannelInfoDestroy(
	IN struct rtmp_adapter *pAd)
{
	if (pAd->pChannelInfo)
	{
/*		kfree(pAd->pChannelInfo); */
		kfree(pAd->pChannelInfo);
		pAd->pChannelInfo = NULL;
	}

	return;
}

/*
	==========================================================================
	Description:
		This routine sets the current PhyMode for calculating
		the dirtyness.
	Return:
		none
	NOTE:
	==========================================================================
 */
void CheckPhyModeIsABand(struct rtmp_adapter *pAd)
{
	pAd->pChannelInfo->IsABand = (WMODE_CAP_5G(pAd->CommonCfg.PhyMode)) ? true : false;

	return;
}


u8 SelectBestChannel(struct rtmp_adapter *pAd, ChannelSel_Alg Alg)
{
	u8 ch = 0;

	/* init pAd->pChannelInfo->IsABand */
	CheckPhyModeIsABand(pAd);

	switch ( Alg )
	{
		case ChannelAlgRandom:
		case ChannelAlgApCnt:
			ch = SelectClearChannelApCnt(pAd);
			break;
		case ChannelAlgCCA:
			ch = SelectClearChannelCCA(pAd);
			break;
		default:
			ch = SelectClearChannelCCA(pAd);
			break;
	}

	RTMPSendWirelessEvent(pAd, IW_CHANNEL_CHANGE_EVENT_FLAG, 0, 0, ch);

	return ch;

}

VOID APAutoChannelInit(struct rtmp_adapter *pAd)
{
	uint32_t BusyTime;

	/* reset bss table */
	AutoChBssTableReset(pAd);

	/* clear Channel Info */
	ChannelInfoReset(pAd);

	/* init pAd->pChannelInfo->IsABand */
	CheckPhyModeIsABand(pAd);

	pAd->ApCfg.current_channel_index = 0;

	/* read clear for primary channel */
	BusyTime = mt76u_reg_read(pAd, CH_BUSY_STA);
}

/*
	==========================================================================
	Description:
		This routine is called at initialization. It returns a channel number
		that complies to regulation domain and less interference with current
		enviornment.
	Return:
		ch -  channel number that
	NOTE:
		The retruned channel number is guaranteed to comply to current regulation
		domain that recorded in pAd->CommonCfg.CountryRegion
        Usage:
               1.) iwpriv ra0 set AutoChannelSel=1
                   Ues the number of AP and inference status to choose
               2.) iwpriv ra0 set AutoChannelSel=2
                   Ues the False CCA count and Rssi to choose
	==========================================================================
 */
u8 APAutoSelectChannel(struct rtmp_adapter *pAd, ChannelSel_Alg Alg)
{
	u8 ch = 0, i;

	/* passive scan channel 1-14. collect statistics */

	/*
		In the autochannel select case. AP didn't get channel yet.
		So have no way to determine which Band AP used by channel number.
	*/

	/* Init some structures before doing AutoChannelSelect() */
	APAutoChannelInit(pAd);

	if (( Alg == ChannelAlgRandom ) && (pAd->pChannelInfo->IsABand == true))
	{   /*for Dfs */
		ch = SelectClearChannelRandom(pAd);
	}
	else
	{

		/*find RSSI in each channel */
		for (i=0; i<pAd->ChannelListNum; i++)
		{
			ULONG wait_time = 200; /* wait for 200 ms at each channel. */

			AsicSwitchChannel(pAd, pAd->ChannelList[i].Channel, true);
			AsicLockChannel(pAd, pAd->ChannelList[i].Channel);/*do nothing */
			pAd->ApCfg.current_channel_index = i;

			pAd->ApCfg.AutoChannel_Channel = pAd->ChannelList[i].Channel;

#ifdef AP_QLOAD_SUPPORT
			/* QLOAD ALARM, ever alarm from QLOAD module */
			if (QLOAD_DOES_ALARM_OCCUR(pAd))
				wait_time = 400;
#endif /* AP_QLOAD_SUPPORT */
			msleep_interruptible(wait_time);

			UpdateChannelInfo(pAd, i,Alg);
		}

		ch = SelectBestChannel(pAd, Alg);
	}

	return ch;
}


#ifdef AP_SCAN_SUPPORT
/*
   ==========================================================================
   Description:
       trigger Auto Channel Selection every period of ACSCheckTime.

   NOTE:
       This function is called in a 1-sec mlme periodic check.
   ==========================================================================
 */
VOID AutoChannelSelCheck(struct rtmp_adapter *pAd)
{
   /* Do nothing if ACSCheckTime is not configured or AP is doing site survey */
   if (pAd->ApCfg.ACSCheckTime == 0 || ApScanRunning(pAd))
       return;
   else
       pAd->ApCfg.ACSCheckCount++;

   if (pAd->ApCfg.ACSCheckCount >= pAd->ApCfg.ACSCheckTime)
   {
       /* Reset Counter */
       pAd->ApCfg.ACSCheckCount = 0;

       /* Do Auto Channel Selection only when no client is associated */
       if (pAd->MacTab.Size != 0)
       {
           DBGPRINT(RT_DEBUG_TRACE,
                   ("%s(): ignore Channel Selection checking because has %u associated clients\n",
                   __FUNCTION__, pAd->MacTab.Size));
           return;
       }
       else
       {
           /* Do Auto Channel Selection */
           DBGPRINT(RT_DEBUG_TRACE,
                   ("%s(): Scanning channels for channel selection.\n", __FUNCTION__));
           ApSiteSurvey(pAd, NULL, SCAN_PASSIVE, true);
       }
   }

}
#endif /* AP_SCAN_SUPPORT */

