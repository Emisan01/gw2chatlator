// kb_chatsource.hpp — the guard between Events: Chat and us (kb_chatsource.cpp).
// Included by kb_shared.hpp at its place (it relies on what stands before it there) – include that, not this.
#pragma once

namespace kb {

// kb_chatsource.cpp: the guard between Events: Chat (another add-on, updated unasked) and us.
struct ChatSourceTest {  // nexus_kb_selftest reads this
    int reading = 0, pause = 0, shown = 0, dropped = 0, unreadable = 0, disabled = 0, decoderFault = 0;
    int received = 0, ignored = 0;
    char version[32] = {}, before[32] = {};
};
bool ReadAddonDefGuarded(void* proc, uint32_t* signature, AddonVersion_t* version);
void ChatSourceLoad();    // g_mu held
void ChatSourceSetVersion(const std::wstring& version);  // g_mu NOT held
void ChatSourceSetReading(bool on, bool muHeld);
void ChatSourceResume();
std::string ChatSourceProtocolText();
void RenderChatSourceRow();      // g_mu NOT held
void RenderChatSourceNotice();   // g_mu held
void RenderChatSourceDetails();  // g_mu held (Options > Technical)
void ChatSourceTestState(ChatSourceTest* t);

}  // namespace kb
