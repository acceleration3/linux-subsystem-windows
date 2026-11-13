#include "selftest/selftest.hpp"

#include "core/context.hpp"
#include "core/desktop_guard.hpp"
#include "core/status_reporter.hpp"
#include "selftest/recording_transport.hpp"
#include "support/display_devices.hpp"
#include "support/target_adapter.hpp"
#include "support/unorm_expect.hpp"

#include <chrono>

namespace lsw::suites::selftest {

void add(Registry& registry) {
    Suite& suite = registry.add_suite("selftest", Policy::Serial);
    suite.add({"selftest.runner.ok", {"internal"}, false, 10, [](Context& context) {
                   context.measure("bitness", context.bitness());
                   return Outcome::ok();
               }});
    suite.add({"selftest.unorm.tie_acceptance", {"internal"}, false, 10, [](Context&) {
                   constexpr UnormRule d3d = UnormRule::Direct3d;
                   constexpr UnormRule khr = UnormRule::Khronos;
                   const bool ties = UnormExpect::accepts8(d3d, 0.5, 127) && UnormExpect::accepts8(d3d, 0.5, 128) && !UnormExpect::accepts8(d3d, 0.5, 126) && !UnormExpect::accepts8(d3d, 0.5, 129) &&
                                     UnormExpect::accepts8(khr, 0.5, 127) && UnormExpect::accepts8(khr, 0.5, 128) && !UnormExpect::accepts8(khr, 0.5, 126) && !UnormExpect::accepts8(khr, 0.5, 129);
                   bool exact = true;
                   for (const UnormRule rule : {d3d, khr}) {
                       exact = exact && UnormExpect::accepts8(rule, 1.0 / 255.0, 1) && !UnormExpect::accepts8(rule, 1.0 / 255.0, 0) && !UnormExpect::accepts8(rule, 1.0 / 255.0, 2) &&
                               UnormExpect::accepts8(rule, 1.0, 255) && !UnormExpect::accepts8(rule, 2.0, 254) && UnormExpect::accepts8(rule, -1.0, 0);
                   }
                   const bool off_tie = UnormExpect::accepts8(d3d, 0.25, 64) && !UnormExpect::accepts8(d3d, 0.25, 63) && UnormExpect::accepts(d3d, 0.5, 2, 1) && UnormExpect::accepts(d3d, 0.5, 2, 2) &&
                                        UnormExpect::accepts8(khr, 0.25, 63) && UnormExpect::accepts8(khr, 0.25, 64) && !UnormExpect::accepts8(khr, 0.25, 62) && !UnormExpect::accepts8(khr, 0.25, 65);
                   const bool within_ulp = UnormExpect::accepts8(d3d, 111.5625 / 255.0, 111) && UnormExpect::accepts8(d3d, 111.5625 / 255.0, 112) && !UnormExpect::accepts8(d3d, 111.5625 / 255.0, 110) &&
                                           UnormExpect::accepts8(d3d, 143.4375 / 255.0, 143) && UnormExpect::accepts8(d3d, 143.4375 / 255.0, 144) && !UnormExpect::accepts8(d3d, 143.4375 / 255.0, 145);
                   return ties && exact && off_tie && within_ulp ? Outcome::ok() : Outcome::fail("the unorm tie helper accepts or refuses a wrong value");
               }});
    suite.add({"selftest.adapter.identity_survives_restart", {"internal"}, false, 10, [](Context&) {
                   const std::vector<std::string> uuids{"aaaa", "bbbb"};
                   std::size_t index = 9;
                   const bool by_uuid = TargetAdapter::find_uuid(uuids, "BB:BB", index) && index == 1;
                   const bool unknown = !TargetAdapter::find_uuid(uuids, "cccc", index) && !TargetAdapter::find_uuid(uuids, "", index);
                   const bool export_stable = TargetAdapter::stable_spec("bbbb", "luid:1") == "uuid:bbbb" && TargetAdapter::stable_spec("", "luid:1") == "luid:1";
                   std::vector<DisplayDevice> devices(2);
                   devices[0].accel = devices[1].accel = true;
                   devices[0].vendor = 0x1111;
                   devices[0].device = 0x2222;
                   devices[1].vendor = 0x3333;
                   devices[1].device = 0x4444;
                   const std::vector<std::uint64_t> ids{(0x3333ull << 32) | 0x4444, (0x1111ull << 32) | 0x2222};
                   std::size_t device = 9;
                   const bool device_of_target = DisplayDevices::match_accel(ids, 0, devices, device) && device == 1 && DisplayDevices::match_accel(ids, 1, devices, device) && device == 0;
                   return by_uuid && unknown && export_stable && device_of_target ? Outcome::ok() : Outcome::fail("the adapter under test is not found again by its stable identity");
               }});
    suite.add({"selftest.desktop.guard_scope", {"internal"}, false, 10, [](Context&) {
                   const std::string own = "C:\\run\\suites";
                   const bool own_kept = !DesktopGuard::foreign("C:\\run\\suites\\lsw-suites64.exe", own) &&
                                         !DesktopGuard::foreign("C:\\Other\\lsw-suites32.exe", own) &&
                                         !DesktopGuard::foreign("c:\\RUN\\Suites\\helper.exe", own);
                   const bool shell_kept = !DesktopGuard::foreign("C:\\Windows\\explorer.exe", own) &&
                                           !DesktopGuard::foreign("C:\\Windows\\SystemApps\\StartMenuExperienceHost.exe", own) &&
                                           !DesktopGuard::foreign("C:\\Windows\\System32\\LogonUI.exe", own);
                   const bool stray = DesktopGuard::foreign("C:\\Apps\\app.exe", own);
                   return own_kept && shell_kept && stray ? Outcome::ok() : Outcome::fail("the desktop guard classifies a process wrongly");
               }});
    suite.add({"selftest.status.batches_carry_plan_sequence_totals", {"internal"}, false, 20, [](Context&) {
                   const auto log = std::make_shared<RecordingTransport::Log>();
                   StatusReporter reporter;
                   StatusReporter::Identity identity;
                   identity.run = "run";
                   identity.part = "part-x64";
                   identity.invocation = "one";
                   reporter.open(std::make_unique<RecordingTransport>(log), identity, {"s.a.one", "s.a.two", "s.b.three"});
                   CheckResult passed;
                   passed.id = "s.a.one";
                   passed.pid = 11;
                   passed.status = Status::Ok;
                   passed.ms = 5;
                   CheckResult failed;
                   failed.id = "s.a.two";
                   failed.pid = 12;
                   failed.status = Status::Fail;
                   failed.reason = "boom";
                   reporter.check_started(passed.id, passed.pid);
                   reporter.check_finished(passed);
                   reporter.check_started(failed.id, failed.pid);
                   reporter.check_finished(failed);
                   reporter.close(true);
                   const std::vector<Json> batches = log->all();
                   if (batches.empty()) {
                       return Outcome::fail("no batch reached the transport");
                   }
                   const Json* begin = batches.front().find("begin");
                   const bool plan = begin != nullptr && begin->find("planned") != nullptr && begin->find("planned")->size() == 3;
                   int starts = 0;
                   int finishes = 0;
                   int ends = 0;
                   bool keyed = true;
                   bool ordered = true;
                   bool reason = false;
                   for (std::size_t i = 0; i < batches.size(); ++i) {
                       const Json& batch = batches[i];
                       keyed = keyed && batch.find("run")->as_string() == "run" && batch.find("part")->as_string() == "part-x64" &&
                               batch.find("invocation")->as_string() == "one" && batch.find("totals") != nullptr;
                       ordered = ordered && batch.find("seq")->as_int() == static_cast<std::int64_t>(i + 1);
                       for (const Json& event : batch.find("events")->values()) {
                           const std::string& kind = event.find("t")->as_string();
                           starts += kind == "start" ? 1 : 0;
                           finishes += kind == "finish" ? 1 : 0;
                           ends += kind == "end" && event.find("complete")->as_bool() ? 1 : 0;
                           reason = reason || (kind == "finish" && event.find("status")->as_string() == "fail" && event.find("reason")->as_string() == "boom");
                       }
                   }
                   const Json& totals = *batches.back().find("totals");
                   const bool counted = totals.find("planned")->as_int() == 3 && totals.find("done")->as_int() == 2 && totals.find("ok")->as_int() == 1 &&
                                        totals.find("fail")->as_int() == 1 && totals.find("running")->as_int() == 0;
                   return plan && keyed && ordered && starts <= 2 && finishes == 2 && ends == 1 && reason && counted
                              ? Outcome::ok()
                              : Outcome::fail("the batches do not carry the plan, the keys, a gapless sequence, every event once and the totals");
               }});
    suite.add({"selftest.status.unknown_part_gets_everything_again", {"internal"}, false, 20, [](Context&) {
                   const auto log = std::make_shared<RecordingTransport::Log>();
                   log->answers = {200, 409, 200};
                   StatusReporter reporter;
                   StatusReporter::Identity identity;
                   identity.run = "run";
                   identity.part = "part-x32";
                   identity.invocation = "one";
                   reporter.open(std::make_unique<RecordingTransport>(log), identity, {"s.a.one", "s.a.two"});
                   if (!log->wait_for(1, 5000)) {
                       return Outcome::fail("the begin record was not sent");
                   }
                   CheckResult first;
                   first.id = "s.a.one";
                   first.pid = 21;
                   first.status = Status::Ok;
                   reporter.check_started(first.id, first.pid);
                   reporter.check_finished(first);
                   reporter.check_started("s.a.two", 22);
                   if (!log->wait_for(3, 5000)) {
                       return Outcome::fail("nothing was sent after the receiver answered that it does not know the part");
                   }
                   const std::vector<Json> batches = log->all();
                   const Json& again = batches[2];
                   const Json* begin = again.find("begin");
                   bool finished = false;
                   bool running = false;
                   for (const Json& event : again.find("events")->values()) {
                       finished = finished || (event.find("t")->as_string() == "finish" && event.find("id")->as_string() == "s.a.one");
                       running = running || (event.find("t")->as_string() == "start" && event.find("id")->as_string() == "s.a.two" && event.find("pid")->as_int() == 22);
                   }
                   const bool refused_plain = batches[1].find("begin") == nullptr && !batches[1].find("snapshot")->as_bool();
                   const bool complete = begin != nullptr && begin->find("planned")->size() == 2 && again.find("snapshot")->as_bool() && again.find("seq")->as_int() == 3 &&
                                         again.find("totals")->find("resends")->as_int() == 1;
                   reporter.close(true);
                   return refused_plain && complete && finished && running
                              ? Outcome::ok()
                              : Outcome::fail("after a 409 the next batch is not the begin record with the plan, every result so far and the running checks");
               }});
    suite.add({"selftest.status.dead_receiver_never_delays_a_check", {"internal"}, false, 30, [](Context& context) {
                   const auto log = std::make_shared<RecordingTransport::Log>();
                   log->answers = {0, 0, 0, 0, 0, 0, 0, 0};
                   log->block_ms = 1500;
                   StatusReporter reporter;
                   StatusReporter::Identity identity;
                   identity.run = "run";
                   identity.part = "part-x64";
                   identity.invocation = "one";
                   const std::size_t count = StatusReporter::kQueueLimit * 2;
                   reporter.open(std::make_unique<RecordingTransport>(log), identity, std::vector<std::string>(count, "s.a.one"));
                   if (!log->wait_for(1, 5000)) {
                       return Outcome::fail("the begin record was not sent");
                   }
                   const auto begin = std::chrono::steady_clock::now();
                   CheckResult result;
                   result.id = "s.a.one";
                   result.status = Status::Ok;
                   for (std::size_t i = 0; i < count; ++i) {
                       result.pid = static_cast<std::uint32_t>(i + 1);
                       reporter.check_started(result.id, result.pid);
                       reporter.check_finished(result);
                   }
                   const auto reported = std::chrono::steady_clock::now();
                   reporter.close(true);
                   const auto closed = std::chrono::steady_clock::now();
                   const long long report_ms = std::chrono::duration_cast<std::chrono::milliseconds>(reported - begin).count();
                   const long long close_ms = std::chrono::duration_cast<std::chrono::milliseconds>(closed - reported).count();
                   context.measure("report_ms", report_ms);
                   context.measure("close_ms", close_ms);
                   if (!log->wait_for(2, 8000)) {
                       return Outcome::fail("nothing was sent after the failed batch");
                   }
                   const Json last = log->all().back();
                   const bool whole = last.find("snapshot")->as_bool() && last.find("begin") != nullptr &&
                                      last.find("totals")->find("done")->as_int() == static_cast<std::int64_t>(count) && last.find("totals")->find("dropped")->as_int() >= 1;
                   return report_ms < log->block_ms && close_ms < StatusReporter::kFlushMs + 1000 && whole
                              ? Outcome::ok()
                              : Outcome::fail("a receiver that does not answer delayed the checks or the exit, or the batch after the failure is not the whole state");
               }});
}

}  // namespace lsw::suites::selftest
