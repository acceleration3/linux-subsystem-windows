#pragma once

#include "lsw/agent/apps/guest_drivers.hpp"

#include <string>

namespace lsw::agent::apps {

// Every process a launch produced, not just the one we started.
//
// The PID ``launch`` returns is only the first process, and for a large
// family of applications it is not the interesting one. Portable builds,
// installers, updaters and most launcher-plus-payload arrangements start
// a small stub which spawns the real executable — often from a different
// directory, under a different name — and then exits. Scoping anything
// to the launched PID means following the stub and losing the
// application: the host sees a splash screen appear and never learns
// which window belongs to what it started.
//
// Each launch is put in a Job Object, which is the exact primitive for
// this: every process a job member creates joins the job, transitively,
// and membership survives the parent exiting. Nothing is ever killed
// through these jobs — the handles carry no limits, so closing one
// leaves its processes running. (An earlier job here did have
// KILL_ON_JOB_CLOSE, and it took the agent's own children down with it
// during a redeploy.)
//
// The job alone is not enough, because a process created on a stub's
// behalf by a broker — the shell, the elevation service — is created by
// the broker and lands outside the job. Photoshop CS6 Portable is one:
// its launcher reaches the real executable through two intermediates,
// both of which have exited by the time the application's window
// appears, and neither the job nor the surviving parent link leads back
// to what we started. So parentage and image location are used as well;
// ``root_of`` describes how the three combine.
class LaunchGroup {
public:
    LaunchGroup() = delete;

    // Put a freshly created process — and everything it goes on to
    // start — under a new job identified by ``pid``. Call before the
    // process runs, or a child it spawns first will be outside.
    // ``process`` is the handle from CreateProcessW; it is not consumed.
    // Returns false if the job could not be created or assigned, which
    // costs descendant tracking and nothing else.
    //
    // ``staging`` is what ``GuestDrivers::stage_for`` placed for this
    // launch. It is unstaged when the launch retires, which is the
    // moment the last process attributed to it exits — the only moment
    // at which the drivers are known to be unused, since the launched
    // PID is a stub that exits long before the application does.
    static bool adopt(void* process, unsigned long pid, GuestDrivers::Staging staging);

    // The name of the Job Object holding ``pid``'s launch.
    //
    // Named rather than anonymous so something outside this process can
    // ask what a launch contains. A window mirror has to know which
    // windows belong to the application it was started for, and the job
    // is the only answer that stays right when the launched process is a
    // stub that exits — the payload it spawned is in the job, whatever
    // its parentage looks like by the time its window appears.
    [[nodiscard]] static std::string job_name(unsigned long pid);

    // The launched PID whose job ``pid`` belongs to, or 0 if it belongs
    // to no launch of ours. A launch's own PID maps to itself.
    //
    // Answers from a snapshot of job membership refreshed on a short
    // timer, because the caller is the window enumeration and asks once
    // per window at poll rate.
    [[nodiscard]] static unsigned long root_of(unsigned long pid);

    // Whether ``root``'s job still has any live process in it. A launch
    // whose stub has exited is still alive by this measure as long as
    // what it started is running — which is what "the application is
    // still open" means for a stub-launched application.
    [[nodiscard]] static bool alive(unsigned long root);
};

}  // namespace lsw::agent::apps
