// office_probe.h - TEST ONLY (office_probe = 1): can a script hand data to the
// plugin? workshop/test/RailPriority_TestOffices.txt keeps technical-office
// positions and worker counts behind marker values in its variables; this
// finds them in memory every 10 s and logs them when they change.
#pragma once

namespace wrsr::office_probe {

void Start();

}  // namespace wrsr::office_probe
