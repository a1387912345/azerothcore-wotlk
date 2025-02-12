/*
 * Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it and/or modify it under version 2 of the License, or (at your option), any later version.
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 */

#include "ChannelMgr.h"
#include "Player.h"
#include "World.h"

ChannelMgr::~ChannelMgr()
{
    for (ChannelMap::iterator itr = channels.begin(); itr != channels.end(); ++itr)
        delete itr->second;

    channels.clear();
}

ChannelMgr* ChannelMgr::forTeam(TeamId teamId)
{
    static ChannelMgr allianceChannelMgr(TEAM_ALLIANCE);
    static ChannelMgr hordeChannelMgr(TEAM_HORDE);

    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHANNEL))
        return &allianceChannelMgr;        // cross-faction

    if (teamId == TEAM_ALLIANCE)
        return &allianceChannelMgr;

    if (teamId == TEAM_HORDE)
        return &hordeChannelMgr;

    return nullptr;
}

void ChannelMgr::LoadChannels()
{
    uint32 oldMSTime = getMSTime();
    uint32 count = 0;

    //                                                    0          1     2     3         4          5
    QueryResult result = CharacterDatabase.PQuery("SELECT channelId, name, team, announce, ownership, password FROM channels ORDER BY channelId ASC");
    if (!result)
    {
        LOG_INFO("server", ">> Loaded 0 channels. DB table `channels` is empty.");
        return;
    }

    std::vector<std::pair<std::string, uint32>> toDelete;
    do
    {
        Field* fields = result->Fetch();

        uint32 channelDBId = fields[0].GetUInt32();
        std::string channelName = fields[1].GetString();
        TeamId team = TeamId(fields[2].GetUInt32());
        std::string password = fields[5].GetString();

        std::wstring channelWName;
        if (!Utf8toWStr(channelName, channelWName))
        {
            LOG_ERROR("server", "Failed to load channel '%s' from database - invalid utf8 sequence? Deleted.", channelName.c_str());
            toDelete.push_back({ channelName, team });
            continue;
        }

        ChannelMgr* mgr = forTeam(team);
        if (!mgr)
        {
            LOG_ERROR("server", "Failed to load custom chat channel '%s' from database - invalid team %u. Deleted.", channelName.c_str(), team);
            toDelete.push_back({ channelName, team });
            continue;
        }

        Channel* newChannel = new Channel(channelName, 0, channelDBId, team, fields[3].GetUInt8(), fields[4].GetUInt8());
        newChannel->SetPassword(password);
        mgr->channels[channelWName] = newChannel;

        if (QueryResult banResult = CharacterDatabase.PQuery("SELECT playerGUID, banTime FROM channels_bans WHERE channelId = %u", channelDBId))
        {
            do
            {
                Field* banFields = banResult->Fetch();
                if (!banFields)
                    break;
                newChannel->AddBan(ObjectGuid::Create<HighGuid::Player>(banFields[0].GetUInt32()), banFields[1].GetUInt32());
            } while (banResult->NextRow());
        }

        if (channelDBId > ChannelMgr::_channelIdMax)
            ChannelMgr::_channelIdMax = channelDBId;
        ++count;
    } while (result->NextRow());

    for (auto pair : toDelete)
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHANNEL);
        stmt->setString(0, pair.first);
        stmt->setUInt32(1, pair.second);
        CharacterDatabase.Execute(stmt);
    }

    LOG_INFO("server", ">> Loaded %u channels in %ums", count, GetMSTimeDiffToNow(oldMSTime));
    LOG_INFO("server", " ");
}

Channel* ChannelMgr::GetJoinChannel(std::string const& name, uint32 channelId)
{
    std::wstring wname;
    Utf8toWStr(name, wname);
    wstrToLower(wname);

    ChannelMap::const_iterator i = channels.find(wname);

    if (i == channels.end())
    {
        Channel* nchan = new Channel(name, channelId, 0, _teamId);
        channels[wname] = nchan;
        return nchan;
    }

    return i->second;
}

Channel* ChannelMgr::GetChannel(std::string const& name, Player* player, bool pkt)
{
    std::wstring wname;
    Utf8toWStr(name, wname);
    wstrToLower(wname);

    ChannelMap::const_iterator i = channels.find(wname);

    if (i == channels.end())
    {
        if (pkt)
        {
            WorldPacket data;
            MakeNotOnPacket(&data, name);
            player->GetSession()->SendPacket(&data);
        }

        return nullptr;
    }

    return i->second;
}

uint32 ChannelMgr::_channelIdMax = 0;
ChannelMgr::ChannelRightsMap ChannelMgr::channels_rights;
ChannelRights ChannelMgr::channelRightsEmpty;

void ChannelMgr::LoadChannelRights()
{
    uint32 oldMSTime = getMSTime();
    channels_rights.clear();

    QueryResult result = CharacterDatabase.Query("SELECT name, flags, speakdelay, joinmessage, delaymessage, moderators FROM channels_rights");
    if (!result)
    {
        LOG_INFO("server", ">>  Loaded 0 Channel Rights!");
        LOG_INFO("server", " ");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        std::set<uint32> moderators;
        const char* moderatorList = fields[5].GetCString();
        if (moderatorList)
        {
            Tokenizer tokens(moderatorList, ' ');
            for (Tokenizer::const_iterator i = tokens.begin(); i != tokens.end(); ++i)
            {
                uint64 moderator_acc = atol(*i);
                if (moderator_acc && ((uint32)moderator_acc) == moderator_acc)
                    moderators.insert((uint32)moderator_acc);
            }
        }

        SetChannelRightsFor(fields[0].GetString(), fields[1].GetUInt32(), fields[2].GetUInt32(), fields[3].GetString(), fields[4].GetString(), moderators);

        ++count;
    } while (result->NextRow());

    LOG_INFO("server", ">> Loaded %d Channel Rights in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    LOG_INFO("server", " ");
}

const ChannelRights& ChannelMgr::GetChannelRightsFor(const std::string& name)
{
    std::string nameStr = name;
    std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
    ChannelRightsMap::const_iterator itr = channels_rights.find(nameStr);
    if (itr != channels_rights.end())
        return itr->second;
    return channelRightsEmpty;
}

void ChannelMgr::SetChannelRightsFor(const std::string& name, const uint32& flags, const uint32& speakDelay, const std::string& joinmessage, const std::string& speakmessage, const std::set<uint32>& moderators)
{
    std::string nameStr = name;
    std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
    channels_rights[nameStr] = ChannelRights(flags, speakDelay, joinmessage, speakmessage, moderators);
}

void ChannelMgr::MakeNotOnPacket(WorldPacket* data, std::string const& name)
{
    data->Initialize(SMSG_CHANNEL_NOTIFY, 1 + name.size());
    (*data) << uint8(5) << name;
}
