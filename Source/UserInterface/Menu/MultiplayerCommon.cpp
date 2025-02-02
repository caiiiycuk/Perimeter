#include "StdAfx.h"
#include "GameShell.h"
#include "AudioPlayer.h"
#include "PerimeterShellUI.h"
#include "MessageBox.h"
#include "../../HT/ht.h"
#include "Localization.h"
#include "MultiplayerCommon.h"
#include "BelligerentSelect.h"
#include "codepages/codepages.h"
#include "MainMenu.h"

//This file handles mostly ingame multiplayer stuff but some parts are shared with main menu

extern LogStream fout;
extern MissionDescription missionToExec;
extern MusicPlayer gb_Music;

struct NetLatencyInfoShell {
    std::string name = {};
    int colorIndex = 0;
    size_t quant = 0;
    size_t ping = 0;
    bool activePlayer = false;
};

std::vector<MissionDescription> multiplayerMaps;
std::vector<MissionDescription> multiplayerSaves;

///Used to pass from logic to render thread
std::vector<LocalizedText> toChatText = {};
std::vector<NetLatencyInfoShell> toNetInfo = {};

void clearMultiplayerVars() {
    toChatText.clear();
}

void loadMultiplayerList() {
    if (multiplayerMaps.empty()) {
        loadMapVector(multiplayerMaps, "RESOURCE/MULTIPLAYER", ".spg");
    }
    if (multiplayerSaves.empty()) {
        loadMapVector(multiplayerSaves, "RESOURCE/SAVES/MULTIPLAYER", ".spg");
    }
}

int SwitchMultiplayerToLoadQuant(float, float ) {
    if (menuChangingDone) {
        gameShell->currentSingleProfile.setLastGameType(UserSingleProfile::MULTIPLAYER);

        clearMultiplayerVars();
        gb_Music.FadeVolume(_fEffectButtonTotalTime*0.001f);
        _shellIconManager.SwitchMenuScreens(SQSH_MM_MULTIPLAYER_LOBBY_SCR, SQSH_MM_LOADING_MISSION_SCR);
        return 0;
    }
    return 1;
}

int SwitchMultiplayerToRestoreQuant(float, float ) {
    if (menuChangingDone) {
        if (missionToExec.gameType_ == GT_MULTI_PLAYER_RESTORE_PARTIAL) {
            MTAutoSingleThread skip_assert;
            universe()->clear();
            universe()->universalLoad(missionToExec, gameShell->savePrm(), nullptr);
            missionToExec.gameType_ = GT_MULTI_PLAYER_LOAD;
            //Report host that we finished
            gameShell->getNetClient()->GameIsReady();
        } else {
            gb_Music.FadeVolume(_fEffectButtonTotalTime * 0.001f);
            HTManager::instance()->GameClose();
            clearMultiplayerVars();
            _shellIconManager.SetModalWnd(0);
            _shellIconManager.LoadControlsGroup(SHELL_LOAD_GROUP_MENU);
            _shellIconManager.SwitchMenuScreens(-1, SQSH_MM_LOADING_MISSION_SCR);
        }
        return 0;
    }
    return 1;
}

int exitMultiplayerGameQuant(float, float ) {
    if (menuChangingDone) {
        //meassge to NetCenter
//		gameShell->getNetClient()->FinishGame();
//		gameShell->getNetClient()->DisconnectAndStartFindHost();
        exitToInterfaceMessage(0);
        return 0;
    }
    return 1;
}

int returnToMultiplayerScreenQuant(float, float ) {
    if (menuChangingDone) {
        int id = _shellIconManager.getVisibleMenuScr();
        if (id == SQSH_MM_MULTIPLAYER_LIST_SCR) {
            gameShell->prepareNetClient();
        } else {
            _shellIconManager.SwitchMenuScreens(LAST_VISIBLE, SQSH_MM_MULTIPLAYER_LIST_SCR);
        }
        return 0;
    }
    return 1;
}

int exitToMultiplayerScreenAction(float, float ) {
    if (!gameShell->isStartedWithMainmenu()) {
        gameShell->GameContinue = false;
    } else if (universe()) {
        hideMessageBox();
        _shellIconManager.AddDynamicHandler(exitMultiplayerGameQuant, CBCODE_QUANT);
    } else {
        hideMessageBox();
        _shellIconManager.AddDynamicHandler(returnToMultiplayerScreenQuant, CBCODE_QUANT);
    }
    return 1;
}

int multiplayerMapNotFoundQuant(float, float ) {
    if (menuChangingDone) {
        gameShell->getNetClient()->Reset();
        showMessageBox();
        return 0;
    }
    return 1;
}

void GameShell::MultiplayerGameStarting() {
    //SHow text message in chat so players know is about to start
    const char* text = qdTextDB::instance().getText("Interface.Menu.Messages.Multiplayer.StartingGame");
    addStringToChatWindow(false, text, getLocale());
    
    //
    const MissionDescription& mission = getNetClient()->getLobbyMissionDescription();
    BELLIGERENT_FACTION faction = FACTION_NONE;
    for (auto& player : mission.playersData) {
        if (mission.activePlayerID == player.playerID) {
            faction = getBelligerentFaction(player.belligerent);
            break;
        }
    }
    if (faction != FACTION_NONE) {
        setBelligerentFactionSound(faction);
        SND2DPlaySound("Frame_Teleportation");
    }
}

void GameShell::MultiplayerGameStart(const MissionDescription& mission) {
    std::string missingContent = checkMissingContent(mission);
    if (!missingContent.empty()) {
        setupOkMessageBox(exitToMultiplayerScreenAction, 0, missingContent, MBOX_EXIT);
        _shellIconManager.AddDynamicHandler(multiplayerMapNotFoundQuant, CBCODE_QUANT);
    } else {
        missionToExec = mission;
        _shellIconManager.AddDynamicHandler(SwitchMultiplayerToLoadQuant, CBCODE_QUANT);
    }
}

void GameShell::MultiplayerGameRestore(const MissionDescription& mission) {
    missionToExec = mission;
    _shellIconManager.AddDynamicHandler(SwitchMultiplayerToRestoreQuant, CBCODE_QUANT);
}

DesyncNotify lastDesyncNotify;

int multiplayerGameDesyncNotifyQuant(float,float) {
    MTAutoSingleThread auto_single;
    PNetCenter* net = gameShell->getNetClient();
    if (lastDesyncNotify.gameID.empty()) {
        return 0;
    }
    
    LocalizedText text(
            qdTextDB::instance().getText("Interface.Menu.Messages.Multiplayer.Nonsinchronization"),
            getLocale()
    );
    text.text += " " + std::to_string(lastDesyncNotify.desync_amount);
    gameShell->serverMessage(&text);

    std::string crash_dir = CRASH_DIR;
    terminate_with_char(crash_dir, PATH_SEP);
    crash_dir += "desync_" + lastDesyncNotify.gameID + "_" + std::to_string(net->m_localNETID) + PATH_SEP;
    create_directories(crash_dir);

    //Write net log
    XBuffer netlog(2048, true);
    netlog < currentVersion < "\r\n";
    netlog < "ArchFlags: " <= computeArchFlags();
    netlog < " HostNETID: " <= net->m_hostNETID;
    netlog < " LocalNETID: " <= net->m_localNETID;
    netlog < " Amount: " <= lastDesyncNotify.desync_amount;
    netlog < "\r\n";
    universe()->writeLogList2Buffer(netlog);
    XStream f(crash_dir + "netlog.txt", XS_OUT);
    f.write(netlog.address(), netlog.tell());
    f.close();
    universe()->clearLogList();

    //Attempt to save state
    gameShell->savePrm().manualData.clearSoundTracks(); //Avoid host overriding client soundtracks
    std::unique_ptr<MissionDescription> md = std::make_unique<MissionDescription>();
    gameShell->universalSave((crash_dir + "save").c_str(), true, md.get());
    md->PrintInfo();

    //Attempt to save reel
    universe()->savePlayReel((crash_dir + "reel").c_str());

    fprintf(stderr, "%d Error network synchronization, dumped at: %s\n", clocki(), crash_dir.c_str());

    //Do not send binary and script data to host except host itself
    //Also trim some data in partial mode
    if (net->m_localNETID != net->m_hostNETID || lastDesyncNotify.desync_amount < PNC_DESYNC_RESTORE_MODE_FULL) {
        md->binaryData.alloc(0);
        md->scriptsData.alloc(0);
    }

    md->setSaveName("");

    //Send the ack
    netCommand4H_DesyncAcknowledge ack(std::move(md));
    std::swap(ack.netlog, netlog);
    net->SendEventSync(&ack);
    return 0;
}

void GameShell::MultiplayerGameDesyncNotify(DesyncNotify& nc) {
    lastDesyncNotify = nc;
    _shellIconManager.AddDynamicHandler(multiplayerGameDesyncNotifyQuant, CBCODE_QUANT);
}

//////////show/hide message ingame////////////////////

std::string messageBoxText;
bool connRestored;

int exitAction( float, float ) {
    hideMessageBox();
    _shellIconManager.AddDynamicHandler(exitMultiplayerGameQuant, CBCODE_QUANT);
    return 1;
}

int showMessageInGameQuant( float, float ) {
    if (menuChangingDone) {
        xassert(_shellIconManager.GetWnd(SQSH_MM_SUBMIT_DIALOG_SCR));
        if (_shellIconManager.GetWnd(SQSH_MM_SUBMIT_DIALOG_SCR)) {
            setupOkMessageBox(exitAction, 0, messageBoxText, MBOX_EXIT);
            gameShell->prepareForInGameMenu();
            showMessageBox();
        }
        return 0;
    }
    return 1;
}

void GameShell::showConnectFailedInGame(const std::string& playerList) {
    messageBoxText = qdTextDB::instance().getText("Interface.Menu.Messages.Multiplayer.WaitingForPlayers");
    messageBoxText += "\n" + playerList;
    _shellIconManager.AddDynamicHandler(showMessageInGameQuant, CBCODE_QUANT);
}

int hideMessageInGameQuant( float, float ) {
    if (menuChangingDone) {
        xassert(_shellIconManager.GetWnd(SQSH_MM_SUBMIT_DIALOG_SCR));
        if (_shellIconManager.GetWnd(SQSH_MM_SUBMIT_DIALOG_SCR)) {
            hideMessageBox();
        }
        if (!connRestored) {
            _shellIconManager.AddDynamicHandler(exitMultiplayerGameQuant, CBCODE_QUANT);
        }
        return 0;
    }
    return 1;
}

void GameShell::hideConnectFailedInGame(bool connectionRestored) {
    connRestored = connectionRestored;
    _shellIconManager.AddDynamicHandler( hideMessageInGameQuant, CBCODE_QUANT );
}

//////abnormalNetCenterTermination/////

int returnToStartScreenQuant( float, float ) {
    if (menuChangingDone) {
        _shellIconManager.SwitchMenuScreens( LAST_VISIBLE, SQSH_MM_START_SCR );
        return 0;
    }
    return 1;
}

int exitToStartScreenAction( float, float ) {
    if (!gameShell->isStartedWithMainmenu()) {
        gameShell->GameContinue = 0;
    } else if (universe()) {
        HTManager::instance()->GameClose();
        _shellIconManager.LoadControlsGroup(SHELL_LOAD_GROUP_MENU);
        _shellIconManager.SwitchMenuScreens(-1, SQSH_MM_START_SCR);
    } else {
        hideMessageBox();
        _shellIconManager.AddDynamicHandler( returnToStartScreenQuant, CBCODE_QUANT );
    }
    return 1;
}

int showTerminationQuant( float, float ) {
    if (menuChangingDone) {
        setupOkMessageBox(exitToStartScreenAction, 0, qdTextDB::instance().getText("Interface.Menu.Messages.UnknownError"), MBOX_EXIT);
        gameShell->prepareForInGameMenu();
        showMessageBox();
        return 0;
    }
    return 1;
}

void GameShell::abnormalNetCenterTermination() {
//	stopNetClient();
    _shellIconManager.AddDynamicHandler(showTerminationQuant, CBCODE_QUANT);
}

//////////generalErrorOccured/////////////

GameShell::GeneralErrorType gtError;

int showGeneralErrorMessageQuant(float, float ) {
    if (menuChangingDone) {
        std::string textID;
        switch (gtError) {
            case GameShell::GENERAL_CONNECTION_FAILED:
                textID = "Interface.Menu.Messages.Multiplayer.ConnectionFailed";
                break;
            case GameShell::KICKED:
                textID = "Interface.Menu.Messages.Multiplayer.Kicked";
                break;
            case GameShell::HOST_TERMINATED:
                textID = "Interface.Menu.Messages.Multiplayer.HostTerminated";
                break;
            case GameShell::DESYNC:
                textID = "Interface.Menu.Messages.Multiplayer.Nonsinchronization";
                break;
            default:
                textID = "Interface.Menu.Messages.UnknownError";
                break;
        }
        
        if (universe()) {
            setupOkMessageBox(nullptr, 0, qdTextDB::instance().getText(textID.c_str()), MBOX_OK);
            gameShell->prepareForInGameMenu();
        } else {
            //setupOkMessageBox(exitToMultiplayerScreenAction, 0, qdTextDB::instance().getText(textID.c_str()), MBOX_BACK);
            setupOkMessageBox(nullptr, 0, qdTextDB::instance().getText(textID.c_str()), MBOX_OK);
        }
        showMessageBox();
        return 0;
    }
    return 1;
}

void GameShell::generalErrorOccured(GeneralErrorType error) {
    fprintf(stderr, "GameShell::generalErrorOccured: 0x%" PRIX32 "\n", static_cast<uint32_t>(error));
    gtError = error;
    _shellIconManager.AddDynamicHandler(showGeneralErrorMessageQuant, CBCODE_QUANT);
}

////////// Lobby/Ingame chat /////////////

void chatWindowInput(CChatInGameEditWindow* chatInput, bool ingame) {
    gameShell->getNetClient()->chatMessage(ingame && chatInput->alliesOnlyMode, chatInput->getText(), getLocale());
    chatInput->SetText("");
}

void onMMLobbyChatInputButton(CShellWindow* pWnd, InterfaceEventCode code, int param) {
    if( code == EVENT_DOUBLECLICK && intfCanHandleInput() ) {
        CChatInGameEditWindow* chatInput = (CChatInGameEditWindow*) pWnd;
        if (!chatInput->isEmptyText()) {
            chatWindowInput(chatInput, false);
        }
    }
}

void onMMInGameChatInputButton(CShellWindow* pWnd, InterfaceEventCode code, int param) {
    if( code == EVENT_DOUBLECLICK && intfCanHandleInput() ) {
        terPlayer* activePlayer = universe()->activePlayer();
        CChatInGameEditWindow* chatInput = (CChatInGameEditWindow*) pWnd;
        if (activePlayer && !activePlayer->shouldIgnoreIntfCommands() && !chatInput->isEmptyText()) {
            chatWindowInput(chatInput, true);
        }
    }
}

int addStringToChatWindowQuant( float, float ) {
    if (!gameShell) {
        return 0;
    }
    ChatWindow* chatWnd = (ChatWindow*)_shellIconManager.GetWnd(SQSH_MM_LOBBY_CHAT_TEXT);
    MTAuto lock(&gameShell->netDataLock);
    if (chatWnd) {
        for (auto& t: toChatText) {
            chatWnd->AddString(&t);
        }
    }
    toChatText.clear();
    return 0;
}

int addStringToChatHintWindowQuant( float, float ) {
    if (!gameShell) {
        return 0;
    }
    MTAuto lock(&gameShell->netDataLock);
    for (auto& t : toChatText) {
        _shellIconManager.addChatString(&t);
    }
    toChatText.clear();
    return 0;
}

void GameShell::addStringToChatWindow(bool clanOnly, const std::string& newString, const std::string& locale) {
    if (_shellIconManager.GetWnd(SQSH_MM_LOBBY_CHAT_TEXT)) {
        {
            MTAuto lock(&netDataLock);
            toChatText.emplace_back(newString, locale);
        }
        _shellIconManager.AddDynamicHandler( addStringToChatWindowQuant, CBCODE_QUANT );
    } else {
        //We add postfix on client side so the text can have local language
        std::string postfix;
        if (clanOnly) {
            //Only add postfix if my locale and msg locale are both russian or both non russian as encoding mixing is not allowed
            if (startsWith(getLocale(), "russian") == startsWith(locale, "russian")) {
                postfix = CChatInGameEditWindow::getModePostfix(clanOnly);
            }
            if (postfix.empty()) {
                //We can't display this text as belongs to another codepage or language has empty text
                //so we just add simple ASCII text which is common in both codepages
                postfix = "clan";
            }
        }
        std::string text = newString;
        if (!postfix.empty()) {
            text.insert(0, " ")
            .insert(0, postfix)
            .insert(0, "&FFFFFF");
        }
        {
            MTAuto lock(&netDataLock);
            toChatText.emplace_back(text, locale);
        }
        _shellIconManager.AddDynamicHandler( addStringToChatHintWindowQuant, CBCODE_QUANT );
    }
}

////////// Latency info ////////////

void CNetLatencyInfoWindow::updateLatencyInfo() {
    briefData.clear();
    fullData.clear();

    if (!_shellIconManager.IsInterface()) {
        return;
    }
    
    std::string mslabel = startsWith(getLocale(), "russian") ? convertToCodepage("мс", "russian") : "ms";

    {
        MTAuto lock(&gameShell->netDataLock);
        for (auto& info : toNetInfo) {
            //Get player name
            if (!fullData.empty()) fullData += "\n";
            std::string name = info.name;
            while (name.length() < PLAYER_MAX_NAME_LEN) {
                name += " ";
            }
            auto& uc = playerColors[info.colorIndex].unitColor;
            fullData += "&" + toColorCode(uc) + name;

            //Get quant text
            std::string quantText = std::to_string(info.quant);
            while (quantText.length() < 2) {
                quantText = " " + quantText;
            }
            sColor4f quantColor = sColor4f(
                    50 < info.quant ? 1 : 0,
                    info.quant < 100 ? 1 : 0,
                    0, 1
            );
            quantText = " &" + toColorCode(quantColor) + quantText;

            //Get ping text
            size_t ping = info.ping;
            ping = static_cast<size_t>(xm::ceil(static_cast<double>(ping) / 1000.0));
            sColor4f pingColor = sColor4f(
                    1000 < ping ? 1 : 0,
                    ping < 2000 ? 1 : 0,
                    0, 1
            );
            std::string pingText = std::to_string(ping);
            while (pingText.length() < 4) {
                pingText = " " + pingText;
            }
            pingText = " &" + toColorCode(pingColor) + pingText + mslabel;

            fullData += quantText + pingText;

            if (info.activePlayer) {
                briefData = qdTextDB::instance().getText("Interface.Menu.ButtonLabels.Network");
                briefData += quantText + pingText;
            }
        }
    }
}

void GameShell::updateLatencyInfo(const NetLatencyInfo& info, const MissionDescription* md) {
    std::vector<NetLatencyInfoShell> newInfo;
    for (int i = 0; i < info.player_ids.size(); ++i) {
        int playerID = info.player_ids[i];
        auto player = md->getPlayerData(playerID);
        if (!player) {
            xassert(0);
            continue;
        }

        auto& shellInfo = newInfo.emplace_back();
        shellInfo.name = player->name();
        shellInfo.colorIndex = player->colorIndex;
        shellInfo.quant = info.quant - info.player_quants[i];
        shellInfo.ping = info.player_pings[i]; // info.timestamp - info.player_last_seen[i];
        shellInfo.activePlayer = playerID == md->activePlayerID;
    }
    {
        MTAuto lock(&netDataLock);
        toNetInfo = newInfo;
    }
}
