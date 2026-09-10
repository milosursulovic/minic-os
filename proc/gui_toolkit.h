// Ring3-side GUI/syscall toolkit (Faza II point 21 onward). Self-contained
// (own syscall wrappers, own arg structs) so it can be #include-d by any
// ring3 program, unlike every ring3 .c file so far which used to
// duplicate this stuff inline with zero sharing. Split across
// proc/gui_toolkit/ into one file per subsystem - this header is just the
// umbrella that includes all of them, so every existing
// #include "gui_toolkit.h" call site keeps working unchanged.

#pragma once

#include "gui_toolkit/core.h"
#include "gui_toolkit/window.h"
#include "gui_toolkit/system.h"
#include "gui_toolkit/file.h"
#include "gui_toolkit/vfs.h"
#include "gui_toolkit/pipe.h"
#include "gui_toolkit/shm.h"
#include "gui_toolkit/socket.h"
#include "gui_toolkit/device.h"
#include "gui_toolkit/service.h"
#include "gui_toolkit/thread.h"
#include "gui_toolkit/sync.h"
#include "gui_toolkit/channel.h"
#include "gui_toolkit/directory.h"
#include "gui_toolkit/users.h"

#include "gui_toolkit/widgets/button.h"
#include "gui_toolkit/widgets/label.h"
#include "gui_toolkit/widgets/checkbox.h"
#include "gui_toolkit/widgets/radio_button.h"
#include "gui_toolkit/widgets/progress_bar.h"
#include "gui_toolkit/widgets/slider.h"
#include "gui_toolkit/widgets/list_view.h"
#include "gui_toolkit/widgets/text_box.h"
