// process_job.hpp — binds child helper processes to a Windows Job Object so a hard crash of the host
// process (without running Unload/Stop) never leaves gw2kb_translate*.exe or gw2kb_voice.exe orphaned.
#pragma once

#include <windows.h>

namespace gct {

inline HANDLE CreateKillOnCloseJob() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

inline bool BindProcessToJobAndResume(HANDLE job, HANDLE process, HANDLE thread) {
    if (job && !AssignProcessToJobObject(job, process)) return false;
    return ResumeThread(thread) != static_cast<DWORD>(-1);
}

}  // namespace gct
