#pragma once

extern "C" {
void sceNpMatching2RegisterContextCallback();
void sceNpMatching2CreateContext();
void sceNpMatching2ContextStart();
void sceNpMatching2Initialize();
void sceNpMatching2GetServerId();
void sceNpMatching2SetDefaultRequestOptParam();
void sceNpMatching2RegisterRoomEventCallback();
void sceNpMatching2RegisterLobbyEventCallback();
void sceNpMatching2RegisterSignalingCallback();
void sceNpMatching2GetWorldInfoList();
}
