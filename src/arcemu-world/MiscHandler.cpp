/*
 * ArcEmu MMORPG Server
 * Copyright (C) 2005-2007 Ascent Team <http://www.ascentemu.com/>
 * Copyright (C) 2008-2012 <http://www.ArcEmu.org/>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "StdAfx.h"

void WorldSession::HandleRepopRequestOpcode(WorldPacket & recv_data)
{
    CHECK_INWORLD_RETURN

    recv_data.read<uint8>();

	LOG_DEBUG("WORLD: Recvd CMSG_REPOP_REQUEST Message");
	if(_player->getDeathState() != JUST_DIED)
		return;
	if(_player->m_CurrentTransporter)
		_player->m_CurrentTransporter->RemovePlayer(_player);

	GetPlayer()->RepopRequestedPlayer();
}

void WorldSession::HandleAutostoreLootItemOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 itemid = 0;
	uint32 amt = 1;
	uint8 lootSlot = 0;
	uint8 error = 0;
	SlotResult slotresult;

	Item* add;
	Loot* pLoot = NULL;

	if(_player->IsCasting())
		_player->InterruptSpell();
	GameObject* pGO = NULL;
	Creature* pCreature = NULL;
	Item *lootSrcItem = NULL;

	uint32 guidtype = GET_TYPE_FROM_GUID(_player->GetLootGUID());
	if(guidtype == HIGHGUID_TYPE_UNIT)
	{
		pCreature = _player->GetMapMgr()->GetCreature(GET_LOWGUID_PART(GetPlayer()->GetLootGUID()));
		if(!pCreature)return;
		pLoot = &pCreature->loot;
	}
	else if(guidtype == HIGHGUID_TYPE_GAMEOBJECT)
	{
		pGO = _player->GetMapMgr()->GetGameObject(GET_LOWGUID_PART(GetPlayer()->GetLootGUID()));
		if(!pGO)return;
		pLoot = &pGO->loot;
	}
	else if(guidtype == HIGHGUID_TYPE_ITEM)
	{
		Item* pItem = _player->GetItemInterface()->GetItemByGUID(_player->GetLootGUID());
		if(!pItem)
			return;
		lootSrcItem = pItem;
		pLoot = pItem->loot;
	}
	else if(guidtype == HIGHGUID_TYPE_PLAYER)
	{
		Player* pl = _player->GetMapMgr()->GetPlayer((uint32)GetPlayer()->GetLootGUID());
		if(!pl) return;
		pLoot = &pl->loot;
	}

	if(!pLoot) return;

	recv_data >> lootSlot;
	if(lootSlot >= pLoot->items.size())
	{
		LOG_DEBUG("Player %s might be using a hack! (slot %d, size %d)",
		          GetPlayer()->GetName(), lootSlot, pLoot->items.size());
		return;
	}

	if( pLoot->items[ lootSlot ].looted ){
		LOG_DEBUG( "Player %s GUID %u tried to loot an already looted item.", _player->GetName(), _player->GetLowGUID() );
		return;
	}

	amt = pLoot->items.at(lootSlot).iItemsCount;
	if(pLoot->items.at(lootSlot).roll != NULL)
		return;

	if(!pLoot->items.at(lootSlot).ffa_loot)
	{
		if(!amt) //Test for party loot
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_ALREADY_LOOTED);
			return;
		}
	}
	else
	{
		//make sure this player can still loot it in case of ffa_loot
		LooterSet::iterator itr = pLoot->items.at(lootSlot).has_looted.find(_player->GetLowGUID());

		if(pLoot->items.at(lootSlot).has_looted.end() != itr)
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_ALREADY_LOOTED);
			return;
		}
	}

	itemid = pLoot->items.at(lootSlot).item.itemproto->ItemId;
	ItemPrototype* it = pLoot->items.at(lootSlot).item.itemproto;

	if((error = _player->GetItemInterface()->CanReceiveItem(it, 1)) != 0)
	{
		_player->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, error);
		return;
	}

	if(pGO)
	{
		CALL_GO_SCRIPT_EVENT(pGO, OnLootTaken)(_player, it);
	}
	else if(pCreature)
		CALL_SCRIPT_EVENT(pCreature, OnLootTaken)(_player, it);

	add = GetPlayer()->GetItemInterface()->FindItemLessMax(itemid, amt, false);
	sHookInterface.OnLoot(_player, pCreature, 0, itemid);
	if(!add)
	{
		slotresult = GetPlayer()->GetItemInterface()->FindFreeInventorySlot(it);
		if(!slotresult.Result)
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_INVENTORY_FULL);
			return;
		}

		LOG_DEBUG("AutoLootItem MISC");
		Item* item = objmgr.CreateItem(itemid, GetPlayer());
		if(item == NULL)
			return;

		item->SetStackCount(amt);
		if(pLoot->items.at(lootSlot).iRandomProperty != NULL)
		{
			item->SetItemRandomPropertyId(pLoot->items.at(lootSlot).iRandomProperty->ID);
			item->ApplyRandomProperties(false);
		}
		else if(pLoot->items.at(lootSlot).iRandomSuffix != NULL)
		{
			item->SetRandomSuffix(pLoot->items.at(lootSlot).iRandomSuffix->id);
			item->ApplyRandomProperties(false);
		}

		if(GetPlayer()->GetItemInterface()->SafeAddItem(item, slotresult.ContainerSlot, slotresult.Slot))
		{
			sQuestMgr.OnPlayerItemPickup(GetPlayer(), item);
			_player->SendItemPushResult(false, true, true, true, slotresult.ContainerSlot, slotresult.Slot, 1, item->GetEntry(), item->GetItemRandomSuffixFactor(), item->GetItemRandomPropertyId(), item->GetStackCount());
#ifdef ENABLE_ACHIEVEMENTS
			_player->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM, item->GetEntry(), 1, 0);
#endif
		}
		else
			item->DeleteMe();
	}
	else
	{
		add->SetStackCount(add->GetStackCount() + amt);
		add->m_isDirty = true;

		sQuestMgr.OnPlayerItemPickup(GetPlayer(), add);
		_player->SendItemPushResult(false, false, true, false, (uint8)_player->GetItemInterface()->GetBagSlotByGuid(add->GetGUID()), 0xFFFFFFFF, amt , add->GetEntry(), add->GetItemRandomSuffixFactor(), add->GetItemRandomPropertyId(), add->GetStackCount());
#ifdef ENABLE_ACHIEVEMENTS
		_player->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM, add->GetEntry(), 1, 0);
#endif
	}

	//in case of ffa_loot update only the player who receives it.
	if(!pLoot->items.at(lootSlot).ffa_loot)
	{
		pLoot->items.at(lootSlot).iItemsCount = 0;

		// this gets sent to all looters
		WorldPacket data(1);
		data.SetOpcode(SMSG_LOOT_REMOVED);
		data << lootSlot;
		Player* plr;
		for(LooterSet::iterator itr = pLoot->looters.begin(); itr != pLoot->looters.end(); ++itr)
		{
			if((plr = _player->GetMapMgr()->GetPlayer(*itr)) != 0)
				plr->GetSession()->SendPacket(&data);
		}
	}
	else
	{
		pLoot->items.at(lootSlot).has_looted.insert(_player->GetLowGUID());
		WorldPacket data(1);
		data.SetOpcode(SMSG_LOOT_REMOVED);
		data << lootSlot;
		_player->GetSession()->SendPacket(&data);
	}

	if( lootSrcItem != NULL ){
		pLoot->items[ lootSlot ].looted = true;
	}

	/* any left yet? (for fishing bobbers) */
	if(pGO && pGO->GetEntry() == GO_FISHING_BOBBER)
	{
		int count = 0;
		for(vector<__LootItem>::iterator itr = pLoot->items.begin(); itr != pLoot->items.end(); ++itr)
			count += (*itr).iItemsCount;
		if(!count)
			pGO->ExpireAndDelete();
	}
}

void WorldSession::HandleLootMoneyOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	Loot* pLoot = NULL;
	uint64 lootguid = GetPlayer()->GetLootGUID();
	if(!lootguid)
		return;   // dunno why this happens

	if(_player->IsCasting())
		_player->InterruptSpell();

	WorldPacket pkt;
	Unit* pt = 0;
	uint32 guidtype = GET_TYPE_FROM_GUID(lootguid);

	if(guidtype == HIGHGUID_TYPE_UNIT)
	{
		Creature* pCreature = _player->GetMapMgr()->GetCreature(GET_LOWGUID_PART(lootguid));
		if(!pCreature)return;
		pLoot = &pCreature->loot;
		pt = pCreature;
	}
	else if(guidtype == HIGHGUID_TYPE_GAMEOBJECT)
	{
		GameObject* pGO = _player->GetMapMgr()->GetGameObject(GET_LOWGUID_PART(lootguid));
		if(!pGO)return;
		pLoot = &pGO->loot;
	}
	else if(guidtype == HIGHGUID_TYPE_CORPSE)
	{
		Corpse* pCorpse = objmgr.GetCorpse((uint32)lootguid);
		if(!pCorpse)return;
		pLoot = &pCorpse->loot;
	}
	else if(guidtype == HIGHGUID_TYPE_PLAYER)
	{
		Player* pPlayer = _player->GetMapMgr()->GetPlayer((uint32)lootguid);
		if(!pPlayer) return;
		pLoot = &pPlayer->loot;
		pPlayer->bShouldHaveLootableOnCorpse = false;
		pt = pPlayer;
	}
	else if(guidtype == HIGHGUID_TYPE_ITEM)
	{
		Item* pItem = _player->GetItemInterface()->GetItemByGUID(lootguid);
		if(!pItem)
			return;
		pLoot = pItem->loot;
	}

	if(!pLoot)
	{
		//bitch about cheating maybe?
		return;
	}

	uint32 money = pLoot->gold;

	pLoot->gold = 0;
	WorldPacket data(1);
	data.SetOpcode(SMSG_LOOT_CLEAR_MONEY);
	// send to all looters
	Player* plr;
	for(LooterSet::iterator itr = pLoot->looters.begin(); itr != pLoot->looters.end(); ++itr)
	{
		if((plr = _player->GetMapMgr()->GetPlayer(*itr)) != 0)
			plr->GetSession()->SendPacket(&data);
	}

	if(!_player->InGroup())
	{
		if(money)
		{
			// Check they don't have more than the max gold
			if(sWorld.GoldCapEnabled && (GetPlayer()->GetGold() + money) > sWorld.GoldLimit)
			{
				GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_TOO_MUCH_GOLD);
			}
			else
			{
				GetPlayer()->ModGold(money);
#ifdef ENABLE_ACHIEVEMENTS
				GetPlayer()->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, money, 0, 0);
#endif
			}
			sHookInterface.OnLoot(_player, pt, money, 0);
		}
	}
	else
	{
		//this code is wrong must be party not raid!
		Group* party = _player->GetGroup();
		if(party)
		{
			/*uint32 share = money/party->MemberCount();*/
			vector<Player*> targets;
			targets.reserve(party->MemberCount());

			GroupMembersSet::iterator itr;
			SubGroup* sgrp;
			party->getLock().Acquire();
			for(uint32 i = 0; i < party->GetSubGroupCount(); i++)
			{
				sgrp = party->GetSubGroup(i);
				for(itr = sgrp->GetGroupMembersBegin(); itr != sgrp->GetGroupMembersEnd(); ++itr)
				{
					if((*itr)->m_loggedInPlayer && (*itr)->m_loggedInPlayer->GetZoneId() == _player->GetZoneId() && _player->GetInstanceID() == (*itr)->m_loggedInPlayer->GetInstanceID())
						targets.push_back((*itr)->m_loggedInPlayer);
				}
			}
			party->getLock().Release();

			if(!targets.size())
				return;

			uint32 share = money / uint32(targets.size());

			pkt.SetOpcode(SMSG_LOOT_MONEY_NOTIFY);
			pkt << share;

			for(vector<Player*>::iterator itr2 = targets.begin(); itr2 != targets.end(); ++itr2)
			{
				// Check they don't have more than the max gold
				if(sWorld.GoldCapEnabled && ((*itr2)->GetGold() + share) > sWorld.GoldLimit)
				{
					(*itr2)->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_TOO_MUCH_GOLD);
				}
				else
				{
					(*itr2)->ModGold(share);
					(*itr2)->GetSession()->SendPacket(&pkt);
#ifdef ENABLE_ACHIEVEMENTS
					(*itr2)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, share, 0, 0);
#endif
				}
			}
		}
	}
}

void WorldSession::HandleLootOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint64 guid;
	recv_data >> guid;

	if(guid == 0)
		return;

	if(_player->IsDead())    // If the player is dead they can't loot!
		return;

	if(_player->IsStealth())    // Check if the player is stealthed
		_player->RemoveStealth(); // cebernic:RemoveStealth on looting. Blizzlike

	if(_player->IsCasting())    // Check if the player is casting
		_player->InterruptSpell(); // Cancel spell casting

	if(_player->IsInvisible())    // Check if the player is invisible for what ever reason
		_player->RemoveInvisibility(); // Remove all invisibility


	if(_player->InGroup() && !_player->m_bg)
	{
		Group* party = _player->GetGroup();
		if(party)
		{
			if(party->GetMethod() == PARTY_LOOT_MASTER)
			{
				WorldPacket data(SMSG_LOOT_MASTER_LIST, 330);  // wont be any larger
				data << (uint8)party->MemberCount();
				uint32 real_count = 0;
				SubGroup* s;
				GroupMembersSet::iterator itr;
				party->Lock();
				for(uint32 i = 0; i < party->GetSubGroupCount(); ++i)
				{
					s = party->GetSubGroup(i);
					for(itr = s->GetGroupMembersBegin(); itr != s->GetGroupMembersEnd(); ++itr)
					{
						if((*itr)->m_loggedInPlayer && _player->GetZoneId() == (*itr)->m_loggedInPlayer->GetZoneId())
						{
							data << (*itr)->m_loggedInPlayer->GetGUID();
							++real_count;
						}
					}
				}
				party->Unlock();
				*(uint8*)&data.contents()[0] = static_cast<uint8>(real_count);

				party->SendPacketToAll(&data);
			}
		}
	}
	_player->SendLoot(guid, LOOT_CORPSE, _player->GetMapId());
}


void WorldSession::HandleLootReleaseOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint64 guid;
	recv_data >> guid;
	WorldPacket data(SMSG_LOOT_RELEASE_RESPONSE, 9);
	data << guid << uint8(1);
	SendPacket(&data);

	_player->SetLootGUID(0);
	_player->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOOTING);
	_player->m_currentLoot = 0;

	if(GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_UNIT)
	{
		Creature* pCreature = _player->GetMapMgr()->GetCreature(GET_LOWGUID_PART(guid));
		if(pCreature == NULL)
			return;
		// remove from looter set
		pCreature->loot.looters.erase(_player->GetLowGUID());
		if(pCreature->loot.gold <= 0)
		{
			for(std::vector<__LootItem>::iterator i = pCreature->loot.items.begin(); i != pCreature->loot.items.end(); ++i)
				if(i->iItemsCount > 0)
				{
					ItemPrototype* proto = i->item.itemproto;
					if(proto->Class != 12)
						return;
					if(_player->HasQuestForItem(i->item.itemproto->ItemId))
						return;
				}
			pCreature->BuildFieldUpdatePacket(_player, OBJECT_FIELD_DYNAMIC_FLAGS, 0);

			if(!pCreature->Skinned)
			{
				if(lootmgr.IsSkinnable(pCreature->GetEntry()))
				{
					pCreature->BuildFieldUpdatePacket(_player, UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE);
				}
			}
		}
	}
	else if(GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_GAMEOBJECT)
	{
		GameObject* pGO = _player->GetMapMgr()->GetGameObject((uint32)guid);
		if(pGO == NULL)
			return;

		switch(pGO->GetType())
		{
			case GAMEOBJECT_TYPE_FISHINGNODE:
				{
					pGO->loot.looters.erase(_player->GetLowGUID());
					if(pGO->IsInWorld())
					{
						pGO->RemoveFromWorld(true);
					}
					delete pGO;
				}
				break;
			case GAMEOBJECT_TYPE_CHEST:
				{
					pGO->loot.looters.erase(_player->GetLowGUID());
					//check for locktypes

					bool despawn = false;
					if( pGO->GetInfo()->sound3 == 1 )
						despawn = true;

					Lock* pLock = dbcLock.LookupEntryForced(pGO->GetInfo()->SpellFocus);
					if(pLock)
					{
						for(uint32 i = 0; i < LOCK_NUM_CASES; i++)
						{
							if(pLock->locktype[i])
							{
								if(pLock->locktype[i] == 1)   //Item or Quest Required;
								{
									if( despawn )
										pGO->Despawn(0, (sQuestMgr.GetGameObjectLootQuest(pGO->GetEntry()) ? 180000 + (RandomUInt(180000)) : 900000 + (RandomUInt(600000))));
									else
										pGO->SetByte( GAMEOBJECT_FIELD_PERCENT_HEALTH, 0, 1 );

									return;
								}
								else if(pLock->locktype[i] == 2)   //locktype;
								{
									//herbalism and mining;
									if(pLock->lockmisc[i] == LOCKTYPE_MINING || pLock->lockmisc[i] == LOCKTYPE_HERBALISM)
									{
										//we still have loot inside.
										if(pGO->HasLoot()  || !despawn )
										{
											pGO->SetByte(GAMEOBJECT_FIELD_PERCENT_HEALTH, 0, 1);
											// TODO : redo this temporary fix, because for some reason hasloot is true even when we loot everything
											// my guess is we need to set up some even that rechecks the GO in 10 seconds or something
											//pGO->Despawn( 600000 + ( RandomUInt( 300000 ) ) );
											return;
										}

										if(pGO->CanMine())
										{
											pGO->loot.items.clear();
											pGO->UseMine();
											return;
										}
										else
										{
											pGO->CalcMineRemaining(true);
											pGO->Despawn(0, 900000 + (RandomUInt(600000)));
											return;
										}
									}
									else
									{
										if(pGO->HasLoot()  || !despawn )
										{
											pGO->SetByte(GAMEOBJECT_FIELD_PERCENT_HEALTH, 0, 1);
											return;
										}
										pGO->Despawn(0, sQuestMgr.GetGameObjectLootQuest(pGO->GetEntry()) ? 180000 + (RandomUInt(180000)) : (IS_INSTANCE(pGO->GetMapId()) ? 0 : 900000 + (RandomUInt(600000))));
										return;
									}
								}
								else //other type of locks that i don't bother to split atm ;P
								{
									if(pGO->HasLoot()  || !despawn )
									{
										pGO->SetByte(GAMEOBJECT_FIELD_PERCENT_HEALTH, 0, 1);
										return;
									}
									pGO->Despawn(0, sQuestMgr.GetGameObjectLootQuest(pGO->GetEntry()) ? 180000 + (RandomUInt(180000)) : (IS_INSTANCE(pGO->GetMapId()) ? 0 : 900000 + (RandomUInt(600000))));
									return;
								}
							}
						}
					}
					else
					{
						if(pGO->HasLoot()  || !despawn )
						{
							pGO->SetByte(GAMEOBJECT_FIELD_PERCENT_HEALTH, 0, 1);
							return;
						}
						pGO->Despawn(0, sQuestMgr.GetGameObjectLootQuest(pGO->GetEntry()) ? 180000 + (RandomUInt(180000)) : (IS_INSTANCE(pGO->GetMapId()) ? 0 : 900000 + (RandomUInt(600000))));

						return;

					}
				}
			default:
				break;
		}
	}
	else if(GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_CORPSE)
	{
		Corpse* pCorpse = objmgr.GetCorpse((uint32)guid);
		if(pCorpse)
			pCorpse->SetUInt32Value(CORPSE_FIELD_DYNAMIC_FLAGS, 0);
	}
	else if(GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_PLAYER)
	{
		Player* plr = objmgr.GetPlayer((uint32)guid);
		if(plr)
		{
			plr->bShouldHaveLootableOnCorpse = false;
			plr->loot.items.clear();
			plr->RemoveFlag(OBJECT_FIELD_DYNAMIC_FLAGS, U_DYN_FLAG_LOOTABLE);
		}
	}
	else if(GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_ITEM)     // Loot from items, eg. sacks, milling, prospecting...
	{
		Item* item = _player->GetItemInterface()->GetItemByGUID(guid);
		if(item == NULL)
			return;

		// delete current loot, so the next one can be filled
		if(item->loot != NULL)
		{
			uint32 itemsNotLooted = 
				std::count_if( item->loot->items.begin(), item->loot->items.end(), ItemIsNotLooted() );

			if( ( itemsNotLooted == 0 ) && ( item->loot->gold == 0 ) ){
				delete item->loot;
				item->loot = NULL;
			}
		}

		// remove loot source items
		if( item->loot == NULL )
			_player->GetItemInterface()->RemoveItemAmtByGuid( guid, 1 );
	}
	else
		LOG_DEBUG("Unhandled loot source object type in HandleLootReleaseOpcode");
}

void WorldSession::HandleWhoOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 min_level;
	uint32 max_level;
	uint32 class_mask;
	uint32 race_mask;
	uint32 zone_count;
	//uint32* zones = 0;
	uint32 name_count;
	//string* names = 0;
	//string chatname;
	//string guildname;
	bool cname = false;
	bool gname = false;
	uint32 i;
    bool showEnemies, exactName, requestServerInfo, showArenaPlayers;
    uint8 playerLen, guildLen, realmNameLen, guildRealmNameLen;
    std::string guildRealmName, playerName, realmName, guildName;
    uint32 zoneIds[10]; // 10 is client limit

    recv_data >> class_mask >> race_mask;
    recv_data >> max_level >> min_level;
    showEnemies = recv_data.ReadBit();
    exactName = recv_data.ReadBit();
    requestServerInfo = recv_data.ReadBit();
    guildRealmNameLen = recv_data.ReadBits(9);
    showArenaPlayers = recv_data.ReadBit();
    playerLen = recv_data.ReadBits(6);
    zone_count = recv_data.ReadBits(4);
    realmNameLen = recv_data.ReadBits(9);
    guildLen = recv_data.ReadBits(7);
    name_count = recv_data.ReadBits(3);

    uint8* wordLens = new uint8[name_count];
    std::string* words = new std::string[name_count];

    for (uint8 i = 0; i < name_count; i++)
        wordLens[i] = recv_data.ReadBits(7);

    recv_data.FlushBits();

    //std::wstring wWords[4];
    for (uint32 i = 0; i < name_count; ++i)
    {
        recv_data >> words[i];

        //std::string temp;
        //recv_data >> temp; // user entered string, it used as universal search pattern(guild+player name)?

        //if (!Utf8toWStr(temp, wWords[i]))
          //  continue;

        //wstrToLower(wWords[i]);
    }

    guildRealmName = recv_data.ReadString(guildRealmNameLen);

    for (uint32 i = 0; i < zone_count; ++i)
        recv_data >> zoneIds[i]; // zone id, 0 if zone is unknown

    playerName = recv_data.ReadString(playerLen);
    realmName = recv_data.ReadString(realmNameLen);
    guildName = recv_data.ReadString(guildLen);

    uint32 virtualRealmAddress = 0;
    int32 faction = 0, locale = 0;

    if (requestServerInfo)
    {
        recv_data >> locale;
        recv_data >> virtualRealmAddress;
        recv_data >> faction;
    }

	if(realmName.length() > 0)
		cname = true;

	if(guildName.length() > 0)
		gname = true;

	LOG_DEBUG("WORLD: Recvd CMSG_WHO Message with %u zones and %u names", zone_count, name_count);

	bool gm = false;
	uint32 team = _player->GetTeam();
	if(HasGMPermissions())
		gm = true;

	uint32 sent_count = 0;
	uint32 total_count = 0;

	PlayerStorageMap::const_iterator itr, iend;
	Player* plr;
	uint32 lvl;
	bool add;

    ByteBuffer bytesData;
	WorldPacket data;
	data.SetOpcode(SMSG_WHO);

    size_t pos = data.bitwpos();
    data.WriteBits(sent_count, 6);

	objmgr._playerslock.AcquireReadLock();
	iend = objmgr._players.end();
	itr = objmgr._players.begin();
	while(itr != iend && sent_count < 49)   // WhoList should display 49 names not including your own
	{
		plr = itr->second;
		++itr;

		if(!plr->GetSession() || !plr->IsInWorld())
			continue;

		if(!sWorld.show_gm_in_who_list && !HasGMPermissions())
		{
			if(plr->GetSession()->HasGMPermissions())
				continue;
		}

		// Team check
		if(!gm && plr->GetTeam() != team && !plr->GetSession()->HasGMPermissions() && !sWorld.interfaction_misc)
			continue;

		++total_count;

		// Add by default, if we don't have any checks
		add = true;

		// Chat name
		//if(cname && chatname != *plr->GetNameString())
			//continue;

		// Guild name
		if(gname)
		{
			if(!plr->GetGuild() || strcmp(plr->GetGuild()->GetGuildName(), guildName.c_str()) != 0)
				continue;
		}

		// Level check
		lvl = plr->getLevel();

		if(min_level && max_level)
		{
			// skip players outside of level range
			if(lvl < min_level || lvl > max_level)
				continue;
		}

		// Zone id compare
		if(zone_count)
		{
			// people that fail the zone check don't get added
			add = false;
			for(i = 0; i < zone_count; ++i)
			{
				if(zoneIds[i] == plr->GetZoneId())
				{
					add = true;
					break;
				}
			}
		}

		if(!((class_mask >> 1) & plr->getClassMask()) || !((race_mask >> 1) & plr->getRaceMask()))
			add = false;

		// skip players that fail zone check
		if(!add)
			continue;

		// name check
		if(name_count)
		{
			// people that fail name check don't get added
			add = false;
			for(i = 0; i < name_count; ++i)
			{
                if (!strnicmp(words[i].c_str(), plr->GetName(), words[i].length()))
				{
					add = true;
					break;
				}
			}
		}

		if(!add)
			continue;

        ObjectGuid playerGuid = plr->GetGUID();
        ObjectGuid accountId = GetAccountId();
        ObjectGuid guildGuid = plr->m_playerInfo->guild ? plr->GetGuild()->GetGuildId() : 0;

        data.WriteBit(accountId[2]);
        data.WriteBit(playerGuid[2]);
        data.WriteBit(accountId[7]);
        data.WriteBit(guildGuid[5]);
        data.WriteBits(guildName.size(), 7);
        data.WriteBit(accountId[1]);
        data.WriteBit(accountId[5]);
        data.WriteBit(guildGuid[7]);
        data.WriteBit(playerGuid[5]);
        data.WriteBit(false);
        data.WriteBit(guildGuid[1]);
        data.WriteBit(playerGuid[6]);
        data.WriteBit(guildGuid[2]);
        data.WriteBit(playerGuid[4]);
        data.WriteBit(guildGuid[0]);
        data.WriteBit(guildGuid[3]);
        data.WriteBit(accountId[6]);
        data.WriteBit(false);
        data.WriteBit(playerGuid[1]);
        data.WriteBit(guildGuid[4]);
        data.WriteBit(accountId[0]);

        for (uint8 i = 0; i < 5; ++i) // MAX_DECLINED_NAME_CASES
            data.WriteBits(0, 7);

        data.WriteBit(playerGuid[3]);
        data.WriteBit(guildGuid[6]);
        data.WriteBit(playerGuid[0]);
        data.WriteBit(accountId[4]);
        data.WriteBit(accountId[3]);
        data.WriteBit(playerGuid[7]);
        data.WriteBits(playerName.size(), 6);

        bytesData.WriteByteSeq(playerGuid[1]);
        bytesData << uint32(1); // realm id
        bytesData.WriteByteSeq(playerGuid[7]);
        bytesData << uint32(1); // realm id
        bytesData.WriteByteSeq(playerGuid[4]);
        bytesData.WriteString(playerName);
        bytesData.WriteByteSeq(guildGuid[1]);
        bytesData.WriteByteSeq(playerGuid[0]);
        bytesData.WriteByteSeq(guildGuid[2]);
        bytesData.WriteByteSeq(guildGuid[0]);
        bytesData.WriteByteSeq(guildGuid[4]);
        bytesData.WriteByteSeq(playerGuid[3]);
        bytesData.WriteByteSeq(guildGuid[6]);
        bytesData << uint32(GetAccountId());
        bytesData.WriteString(guildName);
        bytesData.WriteByteSeq(guildGuid[3]);
        bytesData.WriteByteSeq(accountId[4]);
        bytesData << uint8(plr->getClass());
        bytesData.WriteByteSeq(accountId[7]);
        bytesData.WriteByteSeq(playerGuid[6]);
        bytesData.WriteByteSeq(playerGuid[2]);

        bytesData.WriteByteSeq(accountId[2]);
        bytesData.WriteByteSeq(accountId[3]);
        bytesData << uint8(plr->getRace());
        bytesData.WriteByteSeq(guildGuid[7]);
        bytesData.WriteByteSeq(accountId[1]);
        bytesData.WriteByteSeq(accountId[5]);
        bytesData.WriteByteSeq(accountId[6]);
        bytesData.WriteByteSeq(playerGuid[5]);
        bytesData.WriteByteSeq(accountId[0]);
        bytesData << uint8(plr->getGender());
        bytesData.WriteByteSeq(guildGuid[5]);
        bytesData << uint8(plr->getLevel());
        bytesData << int32(plr->GetZoneId());

		++sent_count;
	}
	objmgr._playerslock.ReleaseReadLock();

    data.FlushBits();
    data.PutBits(pos, sent_count, 6);
    data.append(bytesData);

    SendPacket(&data);

    delete[] words;
    delete[] wordLens;
}

void WorldSession::HandleLogoutRequestOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	Player* pPlayer = GetPlayer();

	WorldPacket data(SMSG_LOGOUT_RESPONSE, 5);

	LOG_DEBUG("WORLD: Recvd CMSG_LOGOUT_REQUEST Message");

	if(pPlayer)
	{
		if (pPlayer->m_isResting || pPlayer->GetTaxiState())
		{
			SetLogoutTimer(1);
			return;
		}

		if(!sHookInterface.OnLogoutRequest(pPlayer))
		{
			data << uint32(1);
			data.WriteBit(0);
			data.FlushBits();
			SendPacket(&data);
			return;
		}

		if(GetPermissionCount() > 0)
		{
			//Logout on NEXT sessionupdate to preserve processing of dead packets (all pending ones should be processed)
			SetLogoutTimer(1);
			return;
		}

		if(pPlayer->CombatStatus.IsInCombat() ||	//can't quit still in combat
		        pPlayer->DuelingWith != NULL)			//can't quit still dueling or attacking
		{
			data << uint32(1);
			data.WriteBit(0);
			data.FlushBits();
			SendPacket(&data);
			return;
		}

		if(pPlayer->m_isResting || pPlayer->GetTaxiState())
		{
			SetLogoutTimer(1);
			return;
		}

		data.WriteBit(0);
		data.FlushBits();
		SendPacket(&data);

		//stop player from moving
		pPlayer->SetMovement(MOVE_ROOT, 1);
		LoggingOut = true;
		// Set the "player locked" flag, to prevent movement
		pPlayer->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOCK_PLAYER);

		//make player sit
		pPlayer->SetStandState(STANDSTATE_SIT);
		SetLogoutTimer(20000);
	}
	/*
	> 0 = You can't Logout Now
	*/
}

void WorldSession::HandlePlayerLogoutOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	LOG_DEBUG("WORLD: Recvd CMSG_PLAYER_LOGOUT Message");
	if(!HasGMPermissions())
	{
		// send "You do not have permission to use this"
		SendNotification(NOTIFICATION_MESSAGE_NO_PERMISSION);
	}
	else
	{
		LogoutPlayer(true);
	}
}

void WorldSession::HandleLogoutCancelOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN
	LOG_DEBUG("WORLD: Recvd CMSG_LOGOUT_CANCEL Message");

	Player* pPlayer = GetPlayer();
	if(!pPlayer)
		return;
	if(!LoggingOut)
		return;
	LoggingOut = false;

	//Cancel logout Timer
	SetLogoutTimer(0);

	//tell client about cancel
	OutPacket(SMSG_LOGOUT_CANCEL_ACK);

	//unroot player
	pPlayer->SetMovement(MOVE_UNROOT, 5);

	// Remove the "player locked" flag, to allow movement
	pPlayer->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOCK_PLAYER);

	//make player stand
	pPlayer->SetStandState(STANDSTATE_STAND);

	LOG_DEBUG("WORLD: sent SMSG_LOGOUT_CANCEL_ACK Message");
}

void WorldSession::HandleZoneUpdateOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN
	uint32 newZone;

	recv_data >> newZone;

	if(GetPlayer()->GetZoneId() == newZone)
		return;

	sWeatherMgr.SendWeather(GetPlayer());
	_player->ZoneUpdate(newZone);

	//clear buyback
	_player->GetItemInterface()->EmptyBuyBack();
}

void WorldSession::HandleSetSelectionOpcode(WorldPacket & recv_data)
{
	ObjectGuid guid;
	guid[7] = recv_data.ReadBit();
	guid[6] = recv_data.ReadBit();
	guid[5] = recv_data.ReadBit();
	guid[4] = recv_data.ReadBit();
	guid[3] = recv_data.ReadBit();
	guid[2] = recv_data.ReadBit();
	guid[1] = recv_data.ReadBit();
	guid[0] = recv_data.ReadBit();

	recv_data.ReadByteSeq(guid[0]);
	recv_data.ReadByteSeq(guid[7]);
	recv_data.ReadByteSeq(guid[3]);
	recv_data.ReadByteSeq(guid[5]);
	recv_data.ReadByteSeq(guid[1]);
	recv_data.ReadByteSeq(guid[4]);
	recv_data.ReadByteSeq(guid[6]);
	recv_data.ReadByteSeq(guid[2]);

	_player->SetSelection(guid);

	if(_player->m_comboPoints)
		_player->UpdateComboPoints();

	_player->SetTargetGUID(guid);
	if(guid == 0) // deselected target
	{
		if(_player->IsInWorld())
			_player->CombatStatusHandler_ResetPvPTimeout();
	}
}

void WorldSession::HandleStandStateChangeOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 animstate;
	recv_data >> animstate;

	_player->SetStandState(animstate);
}

void WorldSession::HandleBugOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 suggestion, contentlen;
	std::string content;
	uint32 typelen;
	std::string type;

	recv_data >> suggestion >> contentlen >> content >> typelen >> type;

	if(suggestion == 0)
		LOG_DEBUG("WORLD: Received CMSG_BUG [Bug Report]");
	else
		LOG_DEBUG("WORLD: Received CMSG_BUG [Suggestion]");

	uint64 AccountId = GetAccountId();
	uint32 TimeStamp = uint32(UNIXTIME);
	uint32 ReportID = objmgr.GenerateReportID();

	std::stringstream ss;

	ss << "INSERT INTO playerbugreports VALUES('";
	ss << ReportID << "','";
	ss << AccountId << "','";
	ss << TimeStamp << "','";
	ss << suggestion << "','";
	ss << CharacterDatabase.EscapeString(type) << "','";
	ss << CharacterDatabase.EscapeString(content) << "')";

	CharacterDatabase.ExecuteNA(ss.str().c_str());
}

void WorldSession::HandleCorpseReclaimOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN
	LOG_DETAIL("WORLD: Received CMSG_RECLAIM_CORPSE");

    //! Not working
    ObjectGuid guid;

    guid[1] = recv_data.ReadBit();
    guid[5] = recv_data.ReadBit();
    guid[7] = recv_data.ReadBit();
    guid[2] = recv_data.ReadBit();
    guid[6] = recv_data.ReadBit();
    guid[3] = recv_data.ReadBit();
    guid[0] = recv_data.ReadBit();
    guid[4] = recv_data.ReadBit();

    recv_data.ReadByteSeq(guid[2]);
    recv_data.ReadByteSeq(guid[5]);
    recv_data.ReadByteSeq(guid[4]);
    recv_data.ReadByteSeq(guid[6]);
    recv_data.ReadByteSeq(guid[1]);
    recv_data.ReadByteSeq(guid[0]);
    recv_data.ReadByteSeq(guid[7]);
    recv_data.ReadByteSeq(guid[3]);
    
    printf("RECLAIM CORPSE FOR GUID %u", guid);

	if(guid == 0)
		return;

	Corpse* pCorpse = objmgr.GetCorpse((uint32)guid);
	if(pCorpse == NULL)	return;

	// Check that we're reviving from a corpse, and that corpse is associated with us.
	if(GET_LOWGUID_PART(pCorpse->GetOwner()) != _player->GetLowGUID() && pCorpse->GetUInt32Value(CORPSE_FIELD_FLAGS) == 5)
	{
		WorldPacket data(SMSG_RESURRECT_FAILED, 4);
		data << uint32(1); // this is a real guess!
		SendPacket(&data);
		return;
	}

	// Check we are actually in range of our corpse
	if(pCorpse->GetDistance2dSq(_player) > CORPSE_MINIMUM_RECLAIM_RADIUS_SQ)
	{
		WorldPacket data(SMSG_RESURRECT_FAILED, 4);
		data << uint32(1);
		SendPacket(&data);
		return;
	}

	// Check death clock before resurrect they must wait for release to complete
	// cebernic: changes for better logic
	if(time(NULL) < pCorpse->GetDeathClock() + CORPSE_RECLAIM_TIME)
	{
		WorldPacket data(SMSG_RESURRECT_FAILED, 4);
		data << uint32(1);
		SendPacket(&data);
		return;
	}

	GetPlayer()->ResurrectPlayer();
	GetPlayer()->SetHealth(GetPlayer()->GetMaxHealth() / 2);
}

void WorldSession::HandleResurrectResponseOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	LOG_DETAIL("WORLD: Received CMSG_RESURRECT_RESPONSE");

	if(_player->isAlive())
		return;

	uint64 guid;
	uint8 status;
	recv_data >> guid;
	recv_data >> status;

	// need to check guid
	Player* pl = _player->GetMapMgr()->GetPlayer((uint32)guid);
	if(pl == NULL)
		pl = objmgr.GetPlayer((uint32)guid);

	// checking valid resurrecter fixes exploits
	if(pl == NULL || status != 1 || !_player->m_resurrecter || _player->m_resurrecter != guid)
	{
		_player->m_resurrectHealth = 0;
		_player->m_resurrectMana = 0;
		_player->m_resurrecter = 0;
		return;
	}

	_player->ResurrectPlayer();
	_player->SetMovement(MOVE_UNROOT, 1);
}

void WorldSession::HandleUpdateAccountData(WorldPacket & recv_data)
{
	LOG_DETAIL("WORLD: Received CMSG_UPDATE_ACCOUNT_DATA");

	uint32 timestamp, type, decompressedSize;

	if (!sWorld.m_useAccountData)
		return;

	recv_data >> timestamp;
    recv_data >> decompressedSize;
    type = recv_data.ReadBits(3);

	if (decompressedSize == 0) // erase
	{
		recv_data.rfinish();

		SetAccountData(type, "", 0, decompressedSize);

		WorldPacket data(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, 4 + 4);
		data << uint32(type);
		data << uint32(0);
		SendPacket(&data);

		return;
	}

	if (decompressedSize > 0xFFFF)
	{
		recv_data.rfinish();                   // unnneded warning spam in this case
		LOG_ERROR("UAD: Account data packet too big, size %u", decompressedSize);
		return;
	}

	ByteBuffer dest;
	dest.resize(decompressedSize);

	uLongf realSize = decompressedSize;
	//if (uncompress(dest.contents(), &realSize, recv_data.contents() + recv_data.rpos(), recv_data.size() - recv_data.rpos()) != Z_OK)
	//{
	//	recv_data.rfinish();                   // unnneded warning spam in this case
	//	LOG_ERROR("UAD: Failed to decompress account data");
	//	return;
	//}

	if (type > NUM_ACCOUNT_DATA_TYPES)
	{
		sLog.outError("type larger that NUM_ACCOUNT_DATA_TYPES Type : %u", type);
		return;
	}

	recv_data.rfinish();

	char* adataa;
	std::string adata;
	dest >> adata;
	adata = adataa; // todo

	SetAccountData(type, adataa, timestamp, decompressedSize);

	WorldPacket data(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, 4 + 4);
	data << uint32(type);
	data << uint32(0);
	SendPacket(&data);
}

void WorldSession::HandleRequestAccountData(WorldPacket & recv_data)
{
	LOG_DETAIL("WORLD: Received CMSG_REQUEST_ACCOUNT_DATA");

	uint32 id;
	if(!sWorld.m_useAccountData)
		return;
	
    id = recv_data.ReadBits(3);

	if(id > 8)
	{
		// Shit..
		LOG_ERROR("WARNING: Accountdata > 8 (%d) was requested by %s of account %d!", id, GetPlayer()->GetName(), this->GetAccountId());
		return;
	}

	AccountDataEntry* res = GetAccountData(id);
	WorldPacket data;
	data.SetOpcode(SMSG_UPDATE_ACCOUNT_DATA);

    ObjectGuid guid;
    data.WriteBits(id, 3);
    data.WriteBit(guid[5]);
    data.WriteBit(guid[1]);
    data.WriteBit(guid[3]);
    data.WriteBit(guid[7]);
    data.WriteBit(guid[0]);
    data.WriteBit(guid[4]);
    data.WriteBit(guid[2]);
    data.WriteBit(guid[6]);
    
    data.WriteByteSeq(guid[3]);
    data.WriteByteSeq(guid[1]);
    data.WriteByteSeq(guid[5]);
    data << uint32(res->sz);         // decompressed length
		
    uLongf destsize;
	if(res->sz > 200)
	{
	    data.resize(res->sz + 800);  // give us plenty of room to work with..

		if((compress(const_cast<uint8*>(data.contents()) + (sizeof(uint32) * 2), &destsize, (const uint8*)res->data, res->sz)) != Z_OK)
		{
			LOG_ERROR("Error while compressing ACCOUNT_DATA");
			return;
		}

		data.resize(destsize + 8);
	}

    data << uint32(destsize);
    data.append(res->data);
    data.WriteByteSeq(guid[7]);
    data.WriteByteSeq(guid[4]);
    data.WriteByteSeq(guid[0]);
    data.WriteByteSeq(guid[6]);
    data.WriteByteSeq(guid[2]);
    data << uint32(res->Time);

    SendPacket(&data);
}

void WorldSession::HandleSetActionButtonOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

    ObjectGuid buttonStream;
    uint8 slotId;

    recv_data >> slotId;

    buttonStream[7] = recv_data.ReadBit();
    buttonStream[0] = recv_data.ReadBit();
    buttonStream[5] = recv_data.ReadBit();
    buttonStream[2] = recv_data.ReadBit();
    buttonStream[1] = recv_data.ReadBit();
    buttonStream[6] = recv_data.ReadBit();
    buttonStream[3] = recv_data.ReadBit();
    buttonStream[4] = recv_data.ReadBit();

    recv_data.ReadByteSeq(buttonStream[6]);
    recv_data.ReadByteSeq(buttonStream[7]);
    recv_data.ReadByteSeq(buttonStream[3]);
    recv_data.ReadByteSeq(buttonStream[5]);
    recv_data.ReadByteSeq(buttonStream[2]);
    recv_data.ReadByteSeq(buttonStream[1]);
    recv_data.ReadByteSeq(buttonStream[4]);
    recv_data.ReadByteSeq(buttonStream[0]);

	LOG_DEBUG("WORLD: Received CMSG_SET_ACTION_BUTTON");
	uint8 button, misc, type;
	uint16 action;
	recv_data >> button >> action >> misc >> type;
	LOG_DEBUG("BUTTON: %u ACTION: %u TYPE: %u MISC: %u", button, action, type, misc);
	if(action == 0)
	{
		LOG_DEBUG("MISC: Remove action from button %u", button);
		//remove the action button from the db
		GetPlayer()->setAction(button, 0, 0, 0);
	}
	else
	{
		if(button >= PLAYER_ACTION_BUTTON_COUNT)
			return;

		if(type == 64 || type == 65)
		{
			LOG_DEBUG("MISC: Added Macro %u into button %u", action, button);
			GetPlayer()->setAction(button, action, misc, type);
		}
		else if(type == 128)
		{
			LOG_DEBUG("MISC: Added Item %u into button %u", action, button);
			GetPlayer()->setAction(button, action, misc, type);
		}
		else if(type == 0)
		{
			LOG_DEBUG("MISC: Added Spell %u into button %u", action, button);
			GetPlayer()->setAction(button, action, type, misc);
		}
	}

}

void WorldSession::HandleSetWatchedFactionIndexOpcode(WorldPacket & recvPacket)
{
	CHECK_INWORLD_RETURN

	uint32 factionid;
	recvPacket >> factionid;
	GetPlayer()->SetUInt32Value(PLAYER_FIELD_WATCHED_FACTION_INDEX, factionid);
}

void WorldSession::HandleTogglePVPOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	_player->PvPToggle();
}

void WorldSession::HandleAmmoSetOpcode(WorldPacket & recv_data)
{
	/*CHECK_INWORLD_RETURN

	uint32 ammoId;
	recv_data >> ammoId;

	if(!ammoId)
		return;

	ItemPrototype* xproto = ItemPrototypeStorage.LookupEntry(ammoId);
	if(!xproto)
		return;

	if(xproto->Class != ITEM_CLASS_PROJECTILE || GetPlayer()->GetItemInterface()->GetItemCount(ammoId) == 0)
	{
		sCheatLog.writefromsession(GetPlayer()->GetSession(), "Definitely cheating. tried to add %u as ammo.", ammoId);
		GetPlayer()->GetSession()->Disconnect();
		return;
	}

	if(xproto->RequiredLevel)
	{
		if(GetPlayer()->getLevel() < xproto->RequiredLevel)
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_ITEM_RANK_NOT_ENOUGH);
			_player->SetAmmoId(0);
			_player->CalcDamage();
			return;
		}
	}
	if(xproto->RequiredSkill)
	{
		if(!GetPlayer()->_HasSkillLine(xproto->RequiredSkill))
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_ITEM_RANK_NOT_ENOUGH);
			_player->SetAmmoId(0);
			_player->CalcDamage();
			return;
		}

		if(xproto->RequiredSkillRank)
		{
			if(_player->_GetSkillLineCurrent(xproto->RequiredSkill, false) < xproto->RequiredSkillRank)
			{
				GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_ITEM_RANK_NOT_ENOUGH);
				_player->SetAmmoId(0);
				_player->CalcDamage();
				return;
			}
		}
	}
	switch(_player->getClass())
	{
		case PRIEST:  // allowing priest, warlock, mage to equip ammo will mess up wand shoot. stop it.
		case WARLOCK:
		case MAGE:
		case SHAMAN: // these don't get messed up since they don't use wands, but they don't get to use bows/guns/crossbows anyways
		case DRUID:  // we wouldn't want them cheating extra stats from ammo, would we?
		case PALADIN:
		case DEATHKNIGHT:
			_player->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_YOU_CAN_NEVER_USE_THAT_ITEM); // good error message?
			_player->SetAmmoId(0);
			_player->CalcDamage();
			return;
		default:
			_player->SetAmmoId(ammoId);
			_player->CalcDamage();
			break;
	}*/
}

#define OPEN_CHEST 11437

void WorldSession::HandleBarberShopResult(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	LOG_DEBUG("WORLD: CMSG_ALTER_APPEARANCE ");

	uint32 hair, haircolor, facialhairorpiercing;
	recv_data >> hair >> haircolor >> facialhairorpiercing;
	uint32 oldhair = _player->GetByte(PLAYER_BYTES, 2);
	uint32 oldhaircolor = _player->GetByte(PLAYER_BYTES, 3);
	uint32 oldfacial = _player->GetByte(PLAYER_BYTES_2, 0);
	uint32 newhair, newhaircolor, newfacial;
	uint32 cost = 0;
	BarberShopStyleEntry* bbse;

	bbse = dbcBarberShopStyleStore.LookupEntryForced(hair);
	if(!bbse)		return;
	newhair = bbse->type;

	newhaircolor = haircolor;

	bbse = dbcBarberShopStyleStore.LookupEntryForced(facialhairorpiercing);
	if(!bbse)		return;
	newfacial = bbse->type;

	uint32 level = _player->getLevel();
	if(level >= 100)
		level = 100;
	GtBarberShopCostBaseEntry* cutcosts = dbcBarberShopCostStore.LookupEntryForced(level - 1);
	if(!cutcosts)
		return;

	// hair style cost = cutcosts
	// hair color cost = cutcosts * 0.5 or free if hair style changed
	// facial hair cost = cutcosts * 0.75
	if(newhair != oldhair)
	{
		cost += (uint32)cutcosts->cost;
	}
	else if(newhaircolor != oldhaircolor)
	{
		cost += (uint32)(cutcosts->cost) >> 1;
	}
	if(newfacial != oldfacial)
	{
		cost += (uint32)(cutcosts->cost * 0.75f);
	}

    // To-Do: move packet sending to function in Player
	if(!_player->HasGold(cost))
	{
		WorldPacket data(SMSG_BARBER_SHOP_RESULT, 4);
        data << uint32(BARBER_SHOP_NOT_ENOUGH_MONEY);
		SendPacket(&data);
		return;
	}
	WorldPacket data(SMSG_BARBER_SHOP_RESULT, 4);
    data << uint32(BARBER_SHOP_SUCCESS);
	SendPacket(&data);

	_player->SetByte(PLAYER_BYTES, 2, static_cast<uint8>(newhair));
	_player->SetByte(PLAYER_BYTES, 3, static_cast<uint8>(newhaircolor));
	_player->SetByte(PLAYER_BYTES_2, 0, static_cast<uint8>(newfacial));
	_player->ModGold(-(int32)cost);

	_player->SetStandState(0);                              // stand up
#ifdef ENABLE_ACHIEVEMENTS
	_player->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_VISIT_BARBER_SHOP, 1, 0, 0);
	_player->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_AT_BARBER, cost, 0, 0);
#endif
}

// CMSG_GAMEOBJ_USE
void WorldSession::HandleGameObjectUse(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

    ObjectGuid guid;

    guid[6] = recv_data.ReadBit();
    guid[1] = recv_data.ReadBit();
    guid[3] = recv_data.ReadBit();
    guid[4] = recv_data.ReadBit();
    guid[0] = recv_data.ReadBit();
    guid[5] = recv_data.ReadBit();
    guid[7] = recv_data.ReadBit();
    guid[2] = recv_data.ReadBit();

    recv_data.ReadByteSeq(guid[0]);
    recv_data.ReadByteSeq(guid[1]);
    recv_data.ReadByteSeq(guid[6]);
    recv_data.ReadByteSeq(guid[2]);
    recv_data.ReadByteSeq(guid[3]);
    recv_data.ReadByteSeq(guid[4]);
    recv_data.ReadByteSeq(guid[5]);
    recv_data.ReadByteSeq(guid[7]);

	SpellCastTargets targets;
	Spell* spell = NULL;
	SpellEntry* spellInfo = NULL;
    printf("CMSG_GAMEOBJ_USE GUID %u \n", guid);
	LOG_DEBUG("WORLD: CMSG_GAMEOBJ_USE: [GUID %d]", guid);

	GameObject* obj = _player->GetMapMgr()->GetGameObject((uint32)guid);
	if(!obj) return;
	GameObjectInfo* goinfo = obj->GetInfo();
	if(!goinfo) return;

	Player* plyr = GetPlayer();

	CALL_GO_SCRIPT_EVENT(obj, OnActivate)(_player);
	CALL_INSTANCE_SCRIPT_EVENT(_player->GetMapMgr(), OnGameObjectActivate)(obj, _player);

	_player->RemoveStealth(); // cebernic:RemoveStealth due to GO was using. Blizzlike

	uint32 type = obj->GetType();
	switch(type)
	{
		case GAMEOBJECT_TYPE_CHAIR:
			{
				plyr->SafeTeleport(plyr->GetMapId(), plyr->GetInstanceID(), obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), obj->GetOrientation());
				plyr->SetStandState(STANDSTATE_SIT_MEDIUM_CHAIR);
				plyr->m_lastRunSpeed = 0; //counteract mount-bug; reset speed to zero to force update SetPlayerSpeed in next line.
				//plyr->SetSpeeds(RUN,plyr->m_base_runSpeed); <--cebernic : Oh No,this could be wrong. If I have some mods existed,this just on baserunspeed as a fixed value?
				plyr->UpdateSpeed();
			}
			break;
		case GAMEOBJECT_TYPE_BARBER_CHAIR:
			{
				plyr->SafeTeleport(plyr->GetMapId(), plyr->GetInstanceID(), obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), obj->GetOrientation());
				plyr->m_lastRunSpeed = 0; //counteract mount-bug; reset speed to zero to force update SetPlayerSpeed in next line.
				plyr->UpdateSpeed();
				//send barber shop menu to player
				WorldPacket data(SMSG_ENABLE_BARBER_SHOP, 0);
				SendPacket(&data);
				plyr->SetStandState(STANDSTATE_SIT_HIGH_CHAIR);
				//Zack : no idea if this phaseshift is even required
//			WorldPacket data2(SMSG_SET_PHASE_SHIFT, 4);
//			data2 << uint32(0x00000200);
//			plyr->SendMessageToSet(&data2, true);
			}
			break;
		case GAMEOBJECT_TYPE_CHEST://cast da spell
			{
				spellInfo = dbcSpellEntry.LookupEntry(OPEN_CHEST);
				spell = sSpellFactoryMgr.NewSpell(plyr, spellInfo, true, NULL);
				_player->m_currentSpell = spell;
				targets.m_unitTarget = obj->GetGUID();
				spell->prepare(&targets);
			}
			break;
		case GAMEOBJECT_TYPE_FISHINGNODE:
			{
				obj->UseFishingNode(plyr);
			}
			break;
		case GAMEOBJECT_TYPE_DOOR:
			{
				// cebernic modified this state = 0 org =1
				if((obj->GetByte(GAMEOBJECT_FIELD_PERCENT_HEALTH, 0) == 0))  //&& (obj->GetUInt32Value(GAMEOBJECT_FLAGS) == 33) )
					obj->EventCloseDoor();
				else
				{
					obj->SetFlag(GAMEOBJECT_FLAGS, 1);   // lock door
					obj->SetByte(GAMEOBJECT_FIELD_PERCENT_HEALTH, 0, 0);
					sEventMgr.AddEvent(obj, &GameObject::EventCloseDoor, EVENT_GAMEOBJECT_DOOR_CLOSE, 20000, 1, EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
				}
			}
			break;
		case GAMEOBJECT_TYPE_FLAGSTAND:
			{
				// battleground/warsong gulch flag
				if(plyr->m_bg)
					plyr->m_bg->HookFlagStand(plyr, obj);

			}
			break;
		case GAMEOBJECT_TYPE_FLAGDROP:
			{
				// Dropped flag
				if(plyr->m_bg)
					plyr->m_bg->HookFlagDrop(plyr, obj);

			}
			break;
		case GAMEOBJECT_TYPE_QUESTGIVER:
			{
				// Questgiver
				if(obj->HasQuests())
				{
					sQuestMgr.OnActivateQuestGiver(obj, plyr);
				}
			}
			break;
		case GAMEOBJECT_TYPE_SPELLCASTER:
			{
				if(obj->m_summoner != NULL && obj->m_summoner->IsPlayer() && plyr != TO< Player* >(obj->m_summoner))
				{
					if(TO< Player* >(obj->m_summoner)->GetGroup() == NULL)
						break;
					else if(TO< Player* >(obj->m_summoner)->GetGroup() != plyr->GetGroup())
						break;
				}

				SpellEntry* info = dbcSpellEntry.LookupEntryForced(goinfo->SpellFocus);
				if(!info)
					break;
				spell = sSpellFactoryMgr.NewSpell(plyr, info, false, NULL);
				//spell->SpellByOther = true;
				targets.m_targetMask |= TARGET_FLAG_UNIT;
				targets.m_unitTarget = plyr->GetGUID();
				spell->prepare(&targets);
				if(obj->charges > 0 && !--obj->charges)
					obj->ExpireAndDelete();
			}
			break;
		case GAMEOBJECT_TYPE_RITUAL:
			{
				// store the members in the ritual, cast sacrifice spell, and summon.
				uint32 i = 0;
				if(!obj->m_ritualmembers || !obj->m_ritualspell || !obj->m_ritualcaster /*|| !obj->m_ritualtarget*/)
					return;

				for(i = 0; i < goinfo->SpellFocus; i++)
				{
					if(!obj->m_ritualmembers[i])
					{
						obj->m_ritualmembers[i] = plyr->GetLowGUID();
						plyr->SetChannelSpellTargetGUID(obj->GetGUID());
						plyr->SetChannelSpellId(obj->m_ritualspell);
						break;
					}
					else if(obj->m_ritualmembers[i] == plyr->GetLowGUID())
					{
						// we're deselecting :(
						obj->m_ritualmembers[i] = 0;
						plyr->SetChannelSpellId(0);
						plyr->SetChannelSpellTargetGUID(0);
						return;
					}
				}

				if(i == goinfo->SpellFocus - 1)
				{
					obj->m_ritualspell = 0;
					Player* plr;
					for(i = 0; i < goinfo->SpellFocus; i++)
					{
						plr = _player->GetMapMgr()->GetPlayer(obj->m_ritualmembers[i]);
						if(plr)
						{
							plr->SetChannelSpellTargetGUID(0);
							plr->SetChannelSpellId(0);
						}
					}

					SpellEntry* info = NULL;
					if(goinfo->ID == 36727 || goinfo->ID == 194108)   // summon portal
					{
						if(!obj->m_ritualtarget)
							return;
						info = dbcSpellEntry.LookupEntryForced(goinfo->sound1);
						if(!info)
							break;
						Player* target = objmgr.GetPlayer(obj->m_ritualtarget);
						if(target == NULL || !target->IsInWorld())
							return;
						spell = sSpellFactoryMgr.NewSpell(_player->GetMapMgr()->GetPlayer(obj->m_ritualcaster), info, true, NULL);
						targets.m_unitTarget = target->GetGUID();
						spell->prepare(&targets);
					}
					else if(goinfo->ID == 177193)    // doom portal
					{
						Player* psacrifice = NULL;

						// kill the sacrifice player
						psacrifice = _player->GetMapMgr()->GetPlayer(obj->m_ritualmembers[(int)(rand() % (goinfo->SpellFocus - 1))]);
						Player* pCaster = obj->GetMapMgr()->GetPlayer(obj->m_ritualcaster);
						if(!psacrifice || !pCaster)
							return;

						info = dbcSpellEntry.LookupEntryForced(goinfo->sound4);
						if(!info)
							break;
						spell = sSpellFactoryMgr.NewSpell(psacrifice, info, true, NULL);
						targets.m_unitTarget = psacrifice->GetGUID();
						spell->prepare(&targets);

						// summons demon
						info = dbcSpellEntry.LookupEntry(goinfo->sound1);
						spell = sSpellFactoryMgr.NewSpell(pCaster, info, true, NULL);
						SpellCastTargets targets2;
						targets2.m_unitTarget = pCaster->GetGUID();
						spell->prepare(&targets2);
					}
					else if(goinfo->ID == 179944)    // Summoning portal for meeting stones
					{
						plr = _player->GetMapMgr()->GetPlayer(obj->m_ritualtarget);
						if(!plr)
							return;

						Player* pleader = _player->GetMapMgr()->GetPlayer(obj->m_ritualcaster);
						if(!pleader)
							return;

						info = dbcSpellEntry.LookupEntry(goinfo->sound1);
						spell = sSpellFactoryMgr.NewSpell(pleader, info, true, NULL);
						SpellCastTargets targets2(plr->GetGUID());
						spell->prepare(&targets2);

						/* expire the gameobject */
						obj->ExpireAndDelete();
					}
					else if(goinfo->ID == 186811 || goinfo->ID == 181622)
					{
						info = dbcSpellEntry.LookupEntryForced(goinfo->sound1);
						if(info == NULL)
							return;
						spell = sSpellFactoryMgr.NewSpell(_player->GetMapMgr()->GetPlayer(obj->m_ritualcaster), info, true, NULL);
						SpellCastTargets targets2(obj->m_ritualcaster);
						spell->prepare(&targets2);
						obj->ExpireAndDelete();
					}
				}
			}
			break;
		case GAMEOBJECT_TYPE_GOOBER:
			{
				plyr->CastSpell(guid, goinfo->Unknown1, false);

				// show page
				if(goinfo->sound7)
				{
					WorldPacket data(SMSG_GAMEOBJECT_PAGETEXT, 8);
					data << obj->GetGUID();
					plyr->GetSession()->SendPacket(&data);
				}
			}
			break;
		case GAMEOBJECT_TYPE_CAMERA://eye of azora
			{
				/*WorldPacket pkt(SMSG_TRIGGER_CINEMATIC,4);
				pkt << (uint32)1;//i ve found only on such item,id =1
				SendPacket(&pkt);*/

				if(goinfo->Unknown1)
				{
					uint32 cinematicid = goinfo->sound1;
					plyr->GetSession()->OutPacket(SMSG_TRIGGER_CINEMATIC, 4, &cinematicid);
				}
			}
			break;
		case GAMEOBJECT_TYPE_MEETINGSTONE:	// Meeting Stone
			{
				/* Use selection */
				Player* pPlayer = objmgr.GetPlayer((uint32)_player->GetSelection());
				if(!pPlayer || _player->GetGroup() != pPlayer->GetGroup() || !_player->GetGroup())
					return;

				GameObjectInfo* info = GameObjectNameStorage.LookupEntry(179944);
				if(!info)
					return;

				/* Create the summoning portal */
				GameObject* pGo = _player->GetMapMgr()->CreateGameObject(179944);
				pGo->CreateFromProto(179944, _player->GetMapId(), _player->GetPositionX(), _player->GetPositionY(), _player->GetPositionZ(), 0);
				pGo->m_ritualcaster = _player->GetLowGUID();
				pGo->m_ritualtarget = pPlayer->GetLowGUID();
				pGo->m_ritualspell = 18540;	// meh
				pGo->PushToWorld(_player->GetMapMgr());

				/* member one: the (w00t) caster */
				pGo->m_ritualmembers[0] = _player->GetLowGUID();
				_player->SetChannelSpellTargetGUID(pGo->GetGUID());
				_player->SetChannelSpellId(pGo->m_ritualspell);

				/* expire after 2mins*/
				sEventMgr.AddEvent(pGo, &GameObject::_Expire, EVENT_GAMEOBJECT_EXPIRE, 120000, 1, EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
			}
			break;
	}
}

void WorldSession::HandleTutorialFlag(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 iFlag;
	recv_data >> iFlag;

	uint32 wInt = (iFlag / 32);
	uint32 rInt = (iFlag % 32);

    if (wInt >= MAX_ACCOUNT_TUTORIAL_VALUES)
	{
		Disconnect();
		return;
	}

	uint32 tutflag = GetPlayer()->GetTutorialInt(wInt);
	tutflag |= (1 << rInt);
	GetPlayer()->SetTutorialInt(wInt, tutflag);

	LOG_DEBUG("Received Tutorial Flag Set {%u}.", iFlag);
}

void WorldSession::HandleTutorialClear(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

    for (uint32 iI = 0; iI < MAX_ACCOUNT_TUTORIAL_VALUES; iI++)
        GetPlayer()->SetTutorialInt(iI, 0xFFFFFFFF);
}

void WorldSession::HandleTutorialReset(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

    for (uint32 iI = 0; iI < MAX_ACCOUNT_TUTORIAL_VALUES; iI++)
        GetPlayer()->SetTutorialInt(iI, 0x00000000);
}

void WorldSession::HandleSetSheathedOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

    uint32 sheathed;
    bool hasData = false;

    recv_data >> sheathed;
    hasData = recv_data.ReadBit();

    _player->SetByte(UNIT_FIELD_BYTES_2, 0, uint8(sheathed)); // Probably not right
}

void WorldSession::HandlePlayedTimeOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 playedt = (uint32)UNIXTIME - _player->m_playedtime[2];
	uint8 DisplayInChatFrame = 0;

    recv_data >> DisplayInChatFrame;

	LOG_DEBUG("Recieved CMSG_PLAYED_TIME.");
    LOG_DEBUG("Display in chat frame (0 or 1): %lu", DisplayInChatFrame);

	if(playedt)
	{
		_player->m_playedtime[0] += playedt;
		_player->m_playedtime[1] += playedt;
		_player->m_playedtime[2] += playedt;
	}

	WorldPacket data(SMSG_PLAYED_TIME, 4 + 4 + 1);
	data << (uint32)_player->m_playedtime[1]; // Total played time (in seconds)
	data << (uint32)_player->m_playedtime[0]; // Time played at current level (in seconds)
    data << uint8(DisplayInChatFrame);
	SendPacket(&data);

	LOG_DEBUG("Sent SMSG_PLAYED_TIME.");
	LOG_DEBUG("Total: %lu Level: %lu", _player->m_playedtime[1], _player->m_playedtime[0]);
}

void WorldSession::HandleInspectOpcode(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 8);
	CHECK_INWORLD_RETURN;

	uint64 guid;
	uint32 talent_points = 0x0000003D;
	ByteBuffer m_Packed_GUID;
	recv_data >> guid;

	Player* player = _player->GetMapMgr()->GetPlayer((uint32)guid);

	if(player == NULL)
	{
		LOG_ERROR("HandleInspectOpcode: guid was null");
		return;
	}

	_player->SetTargetGUID(guid);
	_player->SetSelection(guid);

	if(_player->m_comboPoints)
		_player->UpdateComboPoints();

//	WorldPacket data( SMSG_INSPECT_TALENT, 4 + talent_points );
	WorldPacket data(SMSG_INSPECT_TALENT, 1000);

	m_Packed_GUID.appendPackGUID(player->GetGUID());
	uint32 guid_size;
	guid_size = (uint32)m_Packed_GUID.size();

	data.append(m_Packed_GUID);
	data << uint32(talent_points);

	data << uint8(player->m_talentSpecsCount);
	data << uint8(player->m_talentActiveSpec);
	for(uint8 s = 0; s < player->m_talentSpecsCount; s++)
	{
		PlayerSpec spec = player->m_specs[s];

		int32 talent_max_rank;
		uint32 talent_tab_id;

		uint8 talent_count = 0;
		size_t pos = data.wpos();
		data << uint8(talent_count); //fake value, will be overwritten at the end

		for(uint32 i = 0; i < 3; ++i)
		{
			talent_tab_id = sWorld.InspectTalentTabPages[player->getClass()][i];

			for(uint32 j = 0; j < dbcTalent.GetNumRows(); ++j)
			{
				TalentEntry const* talent_info = dbcTalent.LookupRowForced(j);

				//LOG_DEBUG( "HandleInspectOpcode: i(%i) j(%i)", i, j );

				if(talent_info == NULL)
					continue;

				//LOG_DEBUG( "HandleInspectOpcode: talent_info->TalentTree(%i) talent_tab_id(%i)", talent_info->TalentTree, talent_tab_id );

				/*if(talent_info->TalentTree != talent_tab_id)
					continue;*/ // should i do that?

				talent_max_rank = -1;
				for(int32 k = 4; k > -1; --k)
				{
					//LOG_DEBUG( "HandleInspectOpcode: k(%i) RankID(%i) HasSpell(%i) TalentTree(%i) Tab(%i)", k, talent_info->RankID[k - 1], player->HasSpell( talent_info->RankID[k - 1] ), talent_info->TalentTree, talent_tab_id );
					if(talent_info->RankID[k] != 0 && player->HasSpell(talent_info->RankID[k]))
					{
						talent_max_rank = k;
						break;
					}
				}

				//LOG_DEBUG( "HandleInspectOpcode: RankID(%i) talent_max_rank(%i)", talent_info->RankID[talent_max_rank-1], talent_max_rank );

				if(talent_max_rank < 0)
					continue;

				data << uint32(talent_info->TalentID);
				data << uint8(talent_max_rank);

				++talent_count;

				//LOG_DEBUG( "HandleInspectOpcode: talent(%i) talent_max_rank(%i) rank_id(%i) talent_index(%i) talent_tab_pos(%i) rank_index(%i) rank_slot(%i) rank_offset(%i) mask(%i)", talent_info->TalentID, talent_max_rank, talent_info->RankID[talent_max_rank-1], talent_index, talent_tab_pos, rank_index, rank_slot, rank_offset , mask);
			}
		}

		data.put<uint8>(pos, talent_count);

		// Send Glyph info
		data << uint8(GLYPHS_COUNT);
		for(uint8 i = 0; i < GLYPHS_COUNT; i++)
		{
			data << uint16(spec.glyphs[i]);
		}
	}

	// ----[ Build the item list with their enchantments ]----
	uint32 slot_mask = 0;
	size_t slot_mask_pos = data.wpos();
	data << uint32(slot_mask);   // VLack: 3.1, this is a mask field, if we send 0 we can skip implementing this for now; here should come the player's enchantments from its items (the ones you would see on the character sheet).

	ItemInterface* iif = player->GetItemInterface();

	for(uint32 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)   // Ideally this goes from 0 to 18 (EQUIPMENT_SLOT_END is 19 at the moment)
	{
		Item* item = iif->GetInventoryItem(static_cast<uint16>(i));

		if(!item)
			continue;

		slot_mask |= (1 << i);

		data << uint32(item->GetEntry());

		uint16 enchant_mask = 0;
		size_t enchant_mask_pos = data.wpos();

		data << uint16(enchant_mask);

		for(uint32 Slot = 0; Slot < MAX_ENCHANTMENT_SLOT; ++Slot) // In UpdateFields.h we have ITEM_FIELD_ENCHANTMENT_1_1 to ITEM_FIELD_ENCHANTMENT_12_1, iterate on them...
		{
			uint32 enchantId = item->GetEnchantmentId(Slot);   // This calculation has to be in sync with Item.cpp line ~614, at the moment it is:    uint32 EnchantBase = Slot * 3 + ITEM_FIELD_ENCHANTMENT_1_1;

			if(!enchantId)
				continue;

			enchant_mask |= (1 << Slot);
			data << uint16(enchantId);
		}

		data.put<uint16>(enchant_mask_pos, enchant_mask);

		data << uint16(0);   // UNKNOWN
		FastGUIDPack(data, item->GetCreatorGUID());  // Usually 0 will do, but if your friend created that item for you, then it is nice to display it when you get inspected.
		data << uint32(0);   // UNKNOWN
	}
	data.put<uint32>(slot_mask_pos, slot_mask);

	SendPacket(&data);
}

void WorldSession::HandleSetActionBarTogglesOpcode(WorldPacket & recvPacket)
{
	CHECK_INWORLD_RETURN

	uint8 cActionBarId;
	recvPacket >> cActionBarId;
	LOG_DEBUG("Received CMSG_SET_ACTIONBAR_TOGGLES for actionbar id %d.", cActionBarId);

	GetPlayer()->SetByte(PLAYER_FIELD_BYTES, 2, cActionBarId);
}

// Handlers for acknowledgement opcodes (removes some 'unknown opcode' flood from the logs)
void WorldSession::HandleAcknowledgementOpcodes(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	switch(recv_data.GetOpcode())
	{
		case CMSG_MOVE_WATER_WALK_ACK:
			_player->m_waterwalk = _player->m_setwaterwalk;
			break;

		case CMSG_MOVE_SET_CAN_FLY_ACK:
			_player->FlyCheat = _player->m_setflycheat;
			break;
	}
}

void WorldSession::HandleSelfResurrectOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 self_res_spell = _player->GetUInt32Value(PLAYER_SELF_RES_SPELL);
	if(self_res_spell)
	{
		SpellEntry* sp = dbcSpellEntry.LookupEntry(self_res_spell);
		Spell* s = sSpellFactoryMgr.NewSpell(_player, sp, true, NULL);
		SpellCastTargets tgt;
		tgt.m_unitTarget = _player->GetGUID();
		s->prepare(&tgt);
	}
}

void WorldSession::HandleRandomRollOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 min, max;
	recv_data >> min >> max;

	LOG_DETAIL("WORLD: Received MSG_RANDOM_ROLL: %u-%u", min, max);

	WorldPacket data(20);
	data.SetOpcode(MSG_RANDOM_ROLL);
	data << min << max;

	uint32 roll;

	if(max > RAND_MAX)
		max = RAND_MAX;

	if(min > max)
		min = max;


	// generate number
	roll = RandomUInt(max - min) + min;

	// append to packet, and guid
	data << roll << _player->GetGUID();

	// send to set
	if(_player->InGroup())
		_player->GetGroup()->SendPacketToAll(&data);
	else
		SendPacket(&data);
}

void WorldSession::HandleLootMasterGiveOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

//	uint8 slot = 0;
	uint32 itemid = 0;
	uint32 amt = 1;
	uint8 error = 0;
	SlotResult slotresult;

	Creature* pCreature = NULL;
	GameObject* pGameObject = NULL; //cebernic added it
	Object* pObj = NULL;
	Loot* pLoot = NULL;
	/* struct:
	{CLIENT} Packet: (0x02A3) CMSG_LOOT_MASTER_GIVE PacketSize = 17
	|------------------------------------------------|----------------|
	|00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F |0123456789ABCDEF|
	|------------------------------------------------|----------------|
	|39 23 05 00 81 02 27 F0 01 7B FC 02 00 00 00 00 |9#....'..{......|
	|00											  |.			   |
	-------------------------------------------------------------------

		uint64 creatureguid
		uint8  slotid
		uint64 target_playerguid

	*/
	uint64 creatureguid, target_playerguid;
	uint8 slotid;
	recv_data >> creatureguid >> slotid >> target_playerguid;

	if(_player->GetGroup() == NULL || _player->GetGroup()->GetLooter() != _player->m_playerInfo)
		return;

	Player* player = _player->GetMapMgr()->GetPlayer((uint32)target_playerguid);
	if(!player)
		return;

	// cheaterz!
	if(_player->GetLootGUID() != creatureguid)
		return;

	//now its time to give the loot to the target player
	if(GET_TYPE_FROM_GUID(GetPlayer()->GetLootGUID()) == HIGHGUID_TYPE_UNIT)
	{
		pCreature = _player->GetMapMgr()->GetCreature(GET_LOWGUID_PART(creatureguid));
		if(!pCreature)return;
		pLoot = &pCreature->loot;
	}
	else if(GET_TYPE_FROM_GUID(GetPlayer()->GetLootGUID()) == HIGHGUID_TYPE_GAMEOBJECT) // cebernic added it support gomastergive
	{
		pGameObject = _player->GetMapMgr()->GetGameObject(GET_LOWGUID_PART(creatureguid));
		if(!pGameObject)return;
		pGameObject->SetByte(GAMEOBJECT_FIELD_PERCENT_HEALTH, 0, 0);
		pLoot = &pGameObject->loot;
	}

	if(!pLoot)
		return;
	if(pCreature)
		pObj = pCreature;
	else
		pObj = pGameObject;

	if(!pObj)
		return;

	if(slotid >= pLoot->items.size())
	{
		LOG_DEBUG("AutoLootItem: Player %s might be using a hack! (slot %d, size %d)",
		          GetPlayer()->GetName(), slotid, pLoot->items.size());
		return;
	}

	amt = pLoot->items.at(slotid).iItemsCount;

	if(!pLoot->items.at(slotid).ffa_loot)
	{
		if(!amt) //Test for party loot
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_ALREADY_LOOTED);
			return;
		}
	}
	else
	{
		//make sure this player can still loot it in case of ffa_loot
		LooterSet::iterator itr = pLoot->items.at(slotid).has_looted.find(player->GetLowGUID());

		if(pLoot->items.at(slotid).has_looted.end() != itr)
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_ALREADY_LOOTED);
			return;
		}
	}

	itemid = pLoot->items.at(slotid).item.itemproto->ItemId;
	ItemPrototype* it = pLoot->items.at(slotid).item.itemproto;

	if((error = player->GetItemInterface()->CanReceiveItem(it, 1)) != 0)
	{
		_player->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, error);
		return;
	}

	if(pCreature)
		CALL_SCRIPT_EVENT(pCreature, OnLootTaken)(player, it);


	slotresult = player->GetItemInterface()->FindFreeInventorySlot(it);
	if(!slotresult.Result)
	{
		GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_INVENTORY_FULL);
		return;
	}

	Item* item = objmgr.CreateItem(itemid, player);
	if(item == NULL)
		return;

	item->SetStackCount(amt);
	if(pLoot->items.at(slotid).iRandomProperty != NULL)
	{
		item->SetItemRandomPropertyId(pLoot->items.at(slotid).iRandomProperty->ID);
		item->ApplyRandomProperties(false);
	}
	else if(pLoot->items.at(slotid).iRandomSuffix != NULL)
	{
		item->SetRandomSuffix(pLoot->items.at(slotid).iRandomSuffix->id);
		item->ApplyRandomProperties(false);
	}

	if(player->GetItemInterface()->SafeAddItem(item, slotresult.ContainerSlot, slotresult.Slot))
	{
		player->SendItemPushResult(false, true, true, true, slotresult.ContainerSlot, slotresult.Slot, 1 , item->GetEntry(), item->GetItemRandomSuffixFactor(), item->GetItemRandomPropertyId(), item->GetStackCount());
		sQuestMgr.OnPlayerItemPickup(player, item);
#ifdef ENABLE_ACHIEVEMENTS
		_player->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM, item->GetEntry(), 1, 0);
#endif
	}
	else
		item->DeleteMe();

	pLoot->items.at(slotid).iItemsCount = 0;

	// this gets sent to all looters
	if(!pLoot->items.at(slotid).ffa_loot)
	{
		pLoot->items.at(slotid).iItemsCount = 0;

		// this gets sent to all looters
		WorldPacket data(1);
		data.SetOpcode(SMSG_LOOT_REMOVED);
		data << slotid;
		Player* plr;
		for(LooterSet::iterator itr = pLoot->looters.begin(); itr != pLoot->looters.end(); ++itr)
		{
			if((plr = _player->GetMapMgr()->GetPlayer(*itr)) != 0)
				plr->GetSession()->SendPacket(&data);
		}
	}
	else
	{
		pLoot->items.at(slotid).has_looted.insert(player->GetLowGUID());
	}
}

void WorldSession::HandleLootRollOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	/* struct:

	{CLIENT} Packet: (0x02A0) CMSG_LOOT_ROLL PacketSize = 13
	|------------------------------------------------|----------------|
	|00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F |0123456789ABCDEF|
	|------------------------------------------------|----------------|
	|11 4D 0B 00 BD 06 01 F0 00 00 00 00 02		  |.M...........   |
	-------------------------------------------------------------------

	uint64 creatureguid
	uint21 slotid
	uint8  choice

	*/
	uint64 creatureguid;
	uint32 slotid;
	uint8 choice;
	recv_data >> creatureguid >> slotid >> choice;

	LootRoll* li = NULL;

	uint32 guidtype = GET_TYPE_FROM_GUID(creatureguid);
	if(guidtype == HIGHGUID_TYPE_GAMEOBJECT)
	{
		GameObject* pGO = _player->GetMapMgr()->GetGameObject((uint32)creatureguid);
		if(!pGO)
			return;
		if(slotid >= pGO->loot.items.size() || pGO->loot.items.size() == 0)
			return;
		if(pGO->GetInfo() && pGO->GetInfo()->Type == GAMEOBJECT_TYPE_CHEST)
			li = pGO->loot.items[slotid].roll;
	}
	else if(guidtype == HIGHGUID_TYPE_UNIT)
	{
		// Creatures
		Creature* pCreature = _player->GetMapMgr()->GetCreature(GET_LOWGUID_PART(creatureguid));
		if(!pCreature)
			return;

		if(slotid >= pCreature->loot.items.size() || pCreature->loot.items.size() == 0)
			return;

		li = pCreature->loot.items[slotid].roll;
	}
	else
		return;

	if(!li)
		return;

	li->PlayerRolled(_player, choice);
}

void WorldSession::HandleOpenItemOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	CHECK_PACKET_SIZE(recv_data, 2);
	int8 slot, containerslot;
	recv_data >> containerslot >> slot;

	Item* pItem = _player->GetItemInterface()->GetInventoryItem(containerslot, slot);
	if(!pItem)
		return;

	// gift wrapping handler
	if(pItem->GetGiftCreatorGUID() && pItem->wrapped_item_id)
	{
		ItemPrototype* it = ItemPrototypeStorage.LookupEntry(pItem->wrapped_item_id);
		if(it == NULL)
			return;

		pItem->SetGiftCreatorGUID(0);
		pItem->SetEntry(pItem->wrapped_item_id);
		pItem->wrapped_item_id = 0;
		pItem->SetProto(it);

		if(it->Bonding == ITEM_BIND_ON_PICKUP)
			pItem->SoulBind();
		else
			pItem->ClearFlags();

		if(it->MaxDurability)
		{
			pItem->SetDurability(it->MaxDurability);
			pItem->SetDurabilityMax(it->MaxDurability);
		}

		pItem->m_isDirty = true;
		pItem->SaveToDB(containerslot, slot, false, NULL);
		return;
	}

	Lock* lock = dbcLock.LookupEntryForced(pItem->GetProto()->LockId);

	uint32 removeLockItems[LOCK_NUM_CASES] = {0, 0, 0, 0, 0, 0, 0, 0};

	if(lock) // locked item
	{
		for(int i = 0; i < LOCK_NUM_CASES; i++)
		{
			if(lock->locktype[i] == 1 && lock->lockmisc[i] > 0)
			{
				int16 slot2 = _player->GetItemInterface()->GetInventorySlotById(lock->lockmisc[i]);
				if(slot2 != ITEM_NO_SLOT_AVAILABLE && slot2 >= INVENTORY_SLOT_ITEM_START && slot2 < INVENTORY_SLOT_ITEM_END)
				{
					removeLockItems[i] = lock->lockmisc[i];
				}
				else
				{
					_player->GetItemInterface()->BuildInventoryChangeError(pItem, NULL, INV_ERR_ITEM_LOCKED);
					return;
				}
			}
			else if(lock->locktype[i] == 2 && pItem->locked)
			{
				_player->GetItemInterface()->BuildInventoryChangeError(pItem, NULL, INV_ERR_ITEM_LOCKED);
				return;
			}
		}
		for(int i = 0; i < LOCK_NUM_CASES; i++)
			if(removeLockItems[i])
				_player->GetItemInterface()->RemoveItemAmt(removeLockItems[i], 1);
	}

	// fill loot
	_player->SetLootGUID(pItem->GetGUID());
	if(!pItem->loot)
	{
		pItem->loot = new Loot;
		lootmgr.FillItemLoot(pItem->loot, pItem->GetEntry());
	}
	_player->SendLoot(pItem->GetGUID(), LOOT_DISENCHANTING, _player->GetMapId());
}

void WorldSession::HandleCompleteCinematic(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	// when a Cinematic is started the player is going to sit down, when its finished its standing up.
	_player->SetStandState(STANDSTATE_STAND);
};

void WorldSession::HandleResetInstanceOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	sInstanceMgr.ResetSavedInstances(_player);
}

void WorldSession::HandleDungeonDifficultyOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 data;
	recv_data >> data;

	// Set dungeon difficulty for us
	_player->iInstanceType = data;
	sInstanceMgr.ResetSavedInstances(_player);

	Group* m_Group = _player->GetGroup();

	// If we have a group and we are the leader then set it for the entire group as well
	if(m_Group && _player->IsGroupLeader())
		m_Group->SetDungeonDifficulty(data);
}

void WorldSession::HandleRaidDifficultyOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 data;
	recv_data >> data;

	// set the raid difficulty for us
	_player->SetRaidDifficulty(data);
	sInstanceMgr.ResetSavedInstances(_player);

	Group* m_Group = _player->GetGroup();

	// if we have a group and we are the leader then set it for the entire group as well
	if(m_Group && _player->IsGroupLeader())
		m_Group->SetRaidDifficulty(data);
}

void WorldSession::HandleSummonResponseOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint64 SummonerGUID;
	uint8 IsClickOk;

	recv_data >> SummonerGUID >> IsClickOk;

	if(!IsClickOk)
		return;
	if(!_player->m_summoner)
	{
		SendNotification(NOTIFICATION_MESSAGE_NO_PERMISSION);
		return;
	}

	if(_player->CombatStatus.IsInCombat())
		return;

	_player->SafeTeleport(_player->m_summonMapId, _player->m_summonInstanceId, _player->m_summonPos);

	_player->m_summoner = _player->m_summonInstanceId = _player->m_summonMapId = 0;
}

void WorldSession::HandleDismountOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN
	LOG_DEBUG("WORLD: Received CMSG_DISMOUNT");

	if(_player->GetTaxiState())
		return;

	_player->Dismount();
}

void WorldSession::HandleSetAutoLootPassOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 on;
	recv_data >> on;

	if(_player->IsInWorld())
		_player->BroadcastMessage(_player->GetSession()->LocalizedWorldSrv(67), on ? _player->GetSession()->LocalizedWorldSrv(68) : _player->GetSession()->LocalizedWorldSrv(69));

	_player->m_passOnLoot = (on != 0) ? true : false;
}

void WorldSession::HandleRemoveGlyph(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	uint32 glyphNum;
	recv_data >> glyphNum;
    //! Don't forget to update this when updating glyphs
	if(glyphNum > 5)
		return; // Glyph doesn't exist
	// Get info
	uint32 glyphId = _player->GetGlyph(glyphNum);
	if(glyphId == 0)
		return;
	GlyphPropertiesEntry* glyph = dbcGlyphPropertiesStore.LookupEntryForced(glyphId);
	if(!glyph)
		return;
	_player->SetGlyph(glyphNum, 0);
	_player->RemoveAllAuras(glyph->SpellID, 0);
	_player->m_specs[_player->m_talentActiveSpec].glyphs[glyphNum] = 0;
	_player->SendTalentsInfo(false);
}

// CMSG_GAMEOBJ_REPORT_USE
void WorldSession::HandleGameobjReportUseOpCode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN;

    ObjectGuid guid;

    guid[4] = recv_data.ReadBit();
    guid[7] = recv_data.ReadBit();
    guid[5] = recv_data.ReadBit();
    guid[3] = recv_data.ReadBit();
    guid[6] = recv_data.ReadBit();
    guid[1] = recv_data.ReadBit();
    guid[2] = recv_data.ReadBit();
    guid[0] = recv_data.ReadBit();

    recv_data.ReadByteSeq(guid[7]);
    recv_data.ReadByteSeq(guid[1]);
    recv_data.ReadByteSeq(guid[6]);
    recv_data.ReadByteSeq(guid[5]);
    recv_data.ReadByteSeq(guid[0]);
    recv_data.ReadByteSeq(guid[3]);
    recv_data.ReadByteSeq(guid[2]);
    recv_data.ReadByteSeq(guid[4]);

    printf("REPORT USE GUID %u \n", guid);

	GameObject* gameobj = _player->GetMapMgr()->GetGameObject((uint32)guid);
	if(gameobj == NULL)
		return;
	sQuestMgr.OnGameObjectActivate(_player, gameobj);

#ifdef ENABLE_ACHIEVEMENTS
	_player->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT, gameobj->GetEntry(), 0, 0);
#endif

	return;
}

void WorldSession::HandleWorldStateUITimerUpdate(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN

	WorldPacket data(SMSG_WORLD_STATE_UI_TIMER_UPDATE, 4);
	data << (uint32)UNIXTIME;
	SendPacket(&data);
}

void WorldSession::HandleSetTaxiBenchmarkOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN
	CHECK_PACKET_SIZE(recv_data, 1);

	uint8 mode;
	recv_data >> mode;

	LOG_DEBUG("Client used \"/timetest %d\" command", mode);
}

void WorldSession::HandleRealmSplitOpcode(WorldPacket & recv_data)
{
	LOG_DEBUG("WORLD: Received CMSG_REALM_SPLIT");

	uint32 unk;
	std::string split_date = "01/01/01";
	recv_data >> unk;

	WorldPacket data(SMSG_REALM_SPLIT, 4 + 4 + split_date.size() + 1);
	data << unk;
	data << uint32(0x00000000);   // realm split state
	// split states:
	// 0x0 realm normal
	// 0x1 realm split
	// 0x2 realm split pending
    data.WriteBits(split_date.size(), 7);
	data.WriteString(split_date);
	SendPacket(&data);
}

void WorldSession::HandleReadyForAccountDataTimesOpcode(WorldPacket & recv_data) // 4.3.4 (cmangos)
{
    // empty opcode
    SendAccountDataTimes(GLOBAL_CACHE_MASK);
}

void WorldSession::HandleUITimeRequestOpcode(WorldPacket & recv_data) // 4.3.4 (cmangos)
{
	// empty opcode
	WorldPacket data(SMSG_UI_TIME, 4);
	data << uint32(time(NULL));
	SendPacket(&data);
}

void WorldSession::HandleTimeSyncRespOpcode(WorldPacket & recv_data) // 4.3.4 (cmangos)
{
	uint32 counter, clientTicks;
	recv_data >> counter >> clientTicks;

	if (counter != _player->m_timeSyncQueue.front())
		LOG_ERROR("Wrong time sync counter from player %s (cheater?)", _player->GetName());

	LOG_DEBUG("Time sync received: counter %u, client ticks %u, time since last sync %u", counter, clientTicks, clientTicks - _player->m_timeSyncClient);

	uint32 ourTicks = clientTicks + (getMSTime() - _player->m_timeSyncServer);

	// diff should be small
	LOG_DEBUG("Our ticks: %u, diff %u, latency %u", ourTicks, ourTicks - clientTicks, GetLatency());

	_player->m_timeSyncClient = clientTicks;
	_player->m_timeSyncQueue.pop();
}

void WorldSession::HandleObjectUpdateFailedOpcode(WorldPacket& recvPacket)
{
    ObjectGuid guid;

    guid[3] = recvPacket.ReadBit();
    guid[5] = recvPacket.ReadBit();
    guid[6] = recvPacket.ReadBit();
    guid[0] = recvPacket.ReadBit();
    guid[1] = recvPacket.ReadBit();
    guid[2] = recvPacket.ReadBit();
    guid[7] = recvPacket.ReadBit();
    guid[4] = recvPacket.ReadBit();

    recvPacket.ReadByteSeq(guid[0]);
    recvPacket.ReadByteSeq(guid[6]);
    recvPacket.ReadByteSeq(guid[5]);
    recvPacket.ReadByteSeq(guid[7]);
    recvPacket.ReadByteSeq(guid[2]);
    recvPacket.ReadByteSeq(guid[1]);
    recvPacket.ReadByteSeq(guid[3]);
    recvPacket.ReadByteSeq(guid[4]);

	LOG_ERROR("Failed to update object for GUID: %u", guid);
}

void WorldSession::HandleRequestHotfixOpcode(WorldPacket & recv_data)
{
    uint32 type, count;
    recv_data >> type;

    count = recv_data.ReadBits(21);

    ObjectGuid* guids = new ObjectGuid[count];

    for (uint32 i = 0; i < count; ++i)
    {
        guids[i][6] = recv_data.ReadBit();
        guids[i][3] = recv_data.ReadBit();
        guids[i][0] = recv_data.ReadBit();
        guids[i][1] = recv_data.ReadBit();
        guids[i][4] = recv_data.ReadBit();
        guids[i][5] = recv_data.ReadBit();
        guids[i][7] = recv_data.ReadBit();
        guids[i][2] = recv_data.ReadBit();
    }

    uint32 entry;

    for (uint32 i = 0; i < count; ++i)
    {
        recv_data.ReadByteSeq(guids[i][1]);
        recv_data >> entry;
        recv_data.ReadByteSeq(guids[i][0]);
        recv_data.ReadByteSeq(guids[i][5]);
        recv_data.ReadByteSeq(guids[i][6]);
        recv_data.ReadByteSeq(guids[i][4]);
        recv_data.ReadByteSeq(guids[i][7]);
        recv_data.ReadByteSeq(guids[i][2]);
        recv_data.ReadByteSeq(guids[i][3]);

        switch (type)
        {
        case DB2_REPLY_BROADCASTTEXT:
                SendBroadcastText(entry);
                break;
        case DB2_REPLY_ITEM:
            SendItemDB2Reply(entry);
            break;
        case DB2_REPLY_ITEM_SPARSE:
            SendItemSparseDB2Reply(entry);
            break;
        default:
            // Disabled because we don't support all hotfix types
            //Log.Error("MiscHandler", "CMSG_REQUEST_HOTFIX: Received unknown hotfix type: %u", type);
            break;
        }
    }

    delete[] guids;
}

void WorldSession::SendItemDB2Reply(uint32 entry)
{
    ItemPrototype* proto = ItemPrototypeStorage.LookupEntry(entry);
    if (!proto)
        return;

    WorldPacket data(SMSG_DB_REPLY);
    data << uint32(entry);
    data << uint32(getMSTime());
    data << uint32(DB2_REPLY_ITEM);

    ByteBuffer buff;
    buff << uint32(proto->ItemId);
    buff << uint32(proto->Class);
    buff << uint32(proto->SubClass);
    buff << int32(0); // Sound override subclass
    buff << uint32(proto->LockMaterial);
    buff << uint32(proto->DisplayInfoID);
    buff << uint32(proto->InventoryType);
    buff << uint32(proto->SheathID);

    data << uint32(buff.size());
    data.append(buff);

    SendPacket(&data);
}

#define MAX_ITEM_PROTO_DAMAGES 2
#define MAX_ITEM_PROTO_SOCKETS 3
#define MAX_ITEM_PROTO_SPELLS  5
#define MAX_ITEM_PROTO_STATS  10

void WorldSession::SendItemSparseDB2Reply(uint32 entry)
{
    ItemPrototype* proto = ItemPrototypeStorage.LookupEntry(entry);
    if (!proto)
        return;

    WorldPacket data(SMSG_DB_REPLY);
    data << uint32(entry);
    data << uint32(getMSTime());
    data << uint32(DB2_REPLY_ITEM_SPARSE);

    ByteBuffer buff;
    buff << uint32(proto->ItemId);
    buff << uint32(proto->Quality);
    buff << uint32(proto->Flags);
    buff << uint32(proto->Flags2);
    buff << uint32(0); // Flags 3
    buff << float(1.0f);
    buff << float(1.0f);
    buff << uint32(proto->MaxCount);
    buff << int32(proto->BuyPrice);
    buff << uint32(proto->SellPrice);
    buff << uint32(proto->InventoryType);
    buff << int32(proto->AllowableClass);
    buff << int32(proto->AllowableRace);
    buff << uint32(proto->ItemLevel);
    buff << uint32(proto->RequiredLevel);
    buff << uint32(proto->RequiredSkill);
    buff << uint32(proto->RequiredSkillRank);
    buff << uint32(0); // Required spell
    buff << uint32(proto->RequiredPlayerRank1); // Honor rank
    buff << uint32(proto->RequiredPlayerRank2); // City rank
    buff << uint32(proto->RequiredFactionStanding); // Reputation faction
    buff << uint32(proto->RequiredFaction); // Reputation rank
    buff << int32(proto->MaxCount);
    buff << int32(0); // Stackable
    buff << uint32(proto->ContainerSlots);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_STATS; ++x)
        buff << uint32(proto->Stats[x].Type);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_STATS; ++x)
        buff << int32(proto->Stats[x].Value);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_STATS; ++x)
        buff << int32(0); // Unk

    for (uint32 x = 0; x < MAX_ITEM_PROTO_STATS; ++x)
        buff << int32(0); // Unk

    buff << uint32(proto->ScalingStatsEntry);
    buff << uint32(0); // Damage type
    buff << uint32(proto->Delay);
    buff << float(40); // Ranged range

    for (uint32 x = 0; x < MAX_ITEM_PROTO_SPELLS; ++x)
        buff << int32(0);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_SPELLS; ++x)
        buff << uint32(0);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_SPELLS; ++x)
        buff << int32(0);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_SPELLS; ++x)
        buff << int32(0);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_SPELLS; ++x)
        buff << uint32(0);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_SPELLS; ++x)
        buff << int32(0);

    buff << uint32(proto->Bonding);

    // item name
    std::string name = proto->Name1;
    buff << uint16(name.length());
    if (name.length())
        buff << name;

    for (uint32 i = 0; i < 3; ++i) // Other 3 names
        buff << uint16(0);

    std::string description = proto->Description;
    buff << uint16(description.length());
    if (description.length())
        buff << description;

    buff << uint32(proto->PageId); // Text
    buff << uint32(proto->PageLanguage);
    buff << uint32(proto->PageMaterial);
    buff << uint32(proto->QuestId);
    buff << uint32(proto->LockId);
    buff << int32(proto->LockMaterial);
    buff << uint32(proto->SheathID);
    buff << int32(proto->RandomPropId);
    buff << int32(proto->RandomSuffixId);
    buff << uint32(proto->ItemSet);

    buff << uint32(0); // Area
    buff << uint32(proto->MapID);
    buff << uint32(proto->BagFamily);
    buff << uint32(proto->TotemCategory);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_SOCKETS; ++x)
        buff << uint32(proto->Sockets[x].SocketColor);

    for (uint32 x = 0; x < MAX_ITEM_PROTO_SOCKETS; ++x)
        buff << uint32(proto->Sockets[x].Unk);

    buff << uint32(proto->SocketBonus);
    buff << uint32(proto->GemProperties);
    buff << float(proto->ArmorDamageModifier);
    buff << int32(proto->ExistingDuration);
    buff << uint32(proto->ItemLimitCategory);
    buff << uint32(proto->HolidayId);
    buff << float(proto->ScalingStatsFlag); // StatScalingFactor
    buff << uint32(0); // CurrencySubstitutionId
    buff << uint32(0); // CurrencySubstitutionCount

    data << uint32(buff.size());
    data.append(buff);

    SendPacket(&data);
}

void WorldSession::SendBroadcastText(uint32 entry)
{
    GossipText* pGossip = pGossip = NpcTextStorage.LookupEntry(entry);
    LocalizedNpcText* lnc = (language > 0) ? sLocalizationMgr.GetLocalizedNpcText(entry, language) : NULL;

    uint16 normalTextLength = 0, altTextLength = 0;

    if (lnc)
    {
        normalTextLength = strlen(lnc->Texts[0][0]) + 1;
        altTextLength = strlen(lnc->Texts[0][1]) + 1;
    }
    else
    {
        normalTextLength = strlen(pGossip->Texts[0].Text[0]) + 1;
        altTextLength = strlen(pGossip->Texts[0].Text[1]) + 1;
    }

    ByteBuffer buffer;
    buffer << uint32(entry);
    buffer << uint32(0); // Language
    buffer << uint16(normalTextLength);

    if (normalTextLength)
    {
        if (lnc)
            buffer << lnc->Texts[0][0];
        else
            buffer << pGossip->Texts[0].Text[0];
    }

    buffer << uint16(altTextLength);

    if (altTextLength)
    {
        if (lnc)
            buffer << lnc->Texts[0][1];
        else
            buffer << pGossip->Texts[0].Text[1];
    }

    for (uint8 i = 0; i < 8; ++i) // Max gossip text options
        buffer << uint32(0);

    buffer << uint32(1);

    WorldPacket data(SMSG_DB_REPLY, 4 + 4 + 4 + buffer.size());
    data << uint32(entry);
    data << uint32(getMSTime());
    data << uint32(DB2_REPLY_BROADCASTTEXT);
    data << uint32(buffer.size());
    data.append(buffer);
    
    SendPacket(&data);
}

void WorldSession::HandleReturnToGraveyardOpcode(WorldPacket & recv_data)
{
    _player->RepopAtGraveyard(_player->GetPositionX(), _player->GetPositionY(), _player->GetPositionZ(), _player->GetMapId());
}

// We do not do anything with this opcode...
void WorldSession::HandleLoadScreenOpcode(WorldPacket & recv_data)
{
    uint32 mapId;

    recv_data >> mapId;
    recv_data.ReadBit();
}