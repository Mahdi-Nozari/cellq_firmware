#pragma once

namespace cellq {
namespace usb {
void begin();
void service();
bool maintenanceActive();
void restoreMaintenanceForm();
}
}
