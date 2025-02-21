# Changelog

## [2.0.0] 2023-nn-nn

Release 2.0.0 contains extensive changes.  Shared library libddcutil is not 
backwards compatible.  

#### Added
- Information about detected displays is optionally saved in file $HOME/.cache/ddcutil/displays, 
  shortening startup time. Options ***--enable-displays-cache*** and ***--disable-displays-cache*** 
  control whether this feature is enabled. 
- Install /usr/lib/modules-load.d/ddcutil.conf. Ensures that driver i2c-dev
  is loaded.
- TODO: udev rules riles
- Command options not of interest to general users are now hidden when help is 
  requested.  Option ***--hh*** exposes them, and implies option ***--help***.
- Option ***--noconfig***. Do not process the configuration file.
- Option ***--verbose***.  If specified on the command line, the options obtained
  from the configuration file are reported.
- Added utility options --f7, --f8, --i2, --s1, --s2, --s3, --s4, --fl1, --fl2
  These options are for temporary use during development. The current use of 
  the utility options is reported by option ***--settings***. 
- Added utility command C1 for temporary use during development.
- Added option ***--enable-mock-data*** for testing
- Option ***--trccall***. Traces the call stack starting with the specified 
  function.  This option applies only to functions for which tracing has been enabled.
- API performance profiling
- Added options ***--ignore-hiddev*** and ***--ignore-usb-vid-pid***
- Added: Sample file nvidia-i2c.conf that can be installed in directory 
  /etc/modprobe.d to sent options sometimes needed by the proprieatry NVidia 
  video driver.

#### Changed
- The dynamic sleep algorithm has been completely rewritten to both dynamically
  increase the sleep-multiplier factor (as needed) and decrease the sleep 
  multiplier factor (insofar as possible).  Data is maintained across program 
  executions in file $HOME/.cache/ddcutil/stats. Option ***-dsa***, or one of 
  its variants such as ***--enable-dsa*** turn it on.
- Option ***--sleep-multiplier***:  0 is now allowed as an argument. Some 
  DisplayPort monitors have been observed to work with this value. 
- Writing to the system log has been generalized. 
  Option ***--syslog <level>*** controls what is written to the system log.
  Recognized levels are NEVER, ERROR, WARN, INFO, and DEBUG.  This option 
  replaces ***--enable-syslog***, ***--disable-syslog***, and ***--trace-to-syslog***. 
- **environment --verbose**: Option ***--quickenv*** skips some slow tests such as 
  use of program i2cdetect.
- **environment --verbose**: extended sysfs scan for ARM SOC devices to explore how 
  those devices use /sys
- Detailed statistics are now maintained on a per-display instead of per-thread basis.
- Option ***--vstats*** includes per-display stas in its reports.  It takes the same
  arguments as ***--stats***. 
- Cached capabilities are not erased by ddcutil calls that are not executed with 
  ***--enable-displays-cache***.  This makes the behavior the same as cached 
  displays and cached performance statistics.
- **environment --verbose** disables caching, reports contents of cached files.
- loosen criteria for when to try fallback methods to read EDID when using USB 
  to communicate with Eizo monitors
- udev rule changes: 
  - install /usr/lib/udev/rules.d/60-ddcutil-usb.rules
  - rename /usr/lib/udev/rules.d/60-ddcutil.rules to 60-ddcutil-i2c.rules
  - update and rename sample rules files installed in /usr/share/data/ddcutil as 
    60-ddcutil-i2c.rules and  60-ddcutil-usb.rules.  The user can modify these 
    files and install them in /etc/udev/rules.d to override the files installed 
    in /usr/lib/udev/rules.d. 

#### Fixed
- More robust checks during display detection to test for misuse of the DDC Null Message
and all zero getvcp response to indicate unsupported features.
- Option ***--help***: Document **ELAPSED** as a recognized statistics class
- ddca_dfr_check_by_dref(): do not return an error if no user defined feature file 
  exists for the monitor
- Recognize (but always report failure for) CHKUSBMON command even when ddcutil not 
  built with USB support.
- Improve reporting of /dev/hiddev* open failures to reduce confusion caused by this
  typically benign error.

### Shared library changes

The shared library **libddcutil** is not backwardly compatible.  
The SONAME is now libddcutil.so.5. The released library file is libddcutil.so.5.0.0.

Library initialization has been reworked extensively to move operations 
that can fail and other aspects that should be under control of the library user
into a new function **ddca_init()**.

This function: 
- controls the level of messages written to the system log
- optionally processes options obtained from the **ddcutil** configuration file
- processes additional options passed as a string
- sets error information for ddca_get_error_detail()

If this function is not called by the user program, any API function that relies on its 
processing invokes **ddca_init()** using arguments such that it never fails, e.g. 
the configuration file is not processed.

Added typedef 
- struct DDCA_Display_Detection_Report

Added functions: 
- ddca_init() See above.
- ddca_register_display_detection_callback(): Registers a function 
  of type DDCA_Display_Hotplug_Func which will be called to inform the client
  of display hotplug events.
- ddca_library_filename():  Returns the fully qualified name of the 
  shared library.

Changed functions:

- ddca_report_display_info() returns DDCA_Status instead of void
- ddca_get_feature_name() implementation restored

Changed semantics:

The semantics of some functions have changed, reflecting
the fact that some statistics are now maintained on a per-display
rather than per-thread basis.

- ddca_set_sleep_multiplier(), ddca_get_sleep_multiplier(). 
  Instead of operating on the current thread, these functions operate on the
  display, if any, open in the current thread.

- ddca_set_default_sleep_multiplier(), ddca_get_default_sleep_multiplier()
  Operate on newly detected displays, not new threads.

Removed functions: 

With the ability to configure libddcutil operation both by the ddcutil 
configuration file and by passing an option string in the ddca_init() arguments, 
several API functions are no longer needed and have been removed: 

Max-tries options:
  - ddca_max_max_tries()
  - ddca_get_max_tries()
  - ddca_set_max_tries()
  - ddca_set_default_sleep_multiplier(), ddca_set_global_sleep_multiplier()
  - ddca_get_default_sleep_multiplier(), ddca_set_global_sleep_multiplier()

Trace options: 
  - ddca_add_traced_function()
  - ddca_add_traced_file()
  - ddca_set_trace_groups()
  - ddca_add_trace_groups()
  - ddca_trace_group_name_to_value()
  - ddca_set_trace_options()

USB enablement: 
  - ddca_enable_usb_display_detection
  - ddca_disable_usb_display_detection 

Miscellaneous: 
  - ddca_enable_force_slave_adress() 
  - ddca_is_force_slave_address_enabled()
  - ddca_enable_error_info()

Most per-thread statistics are now instead maintained on a per-display basis.
The following functions are no longer useful and have been removed
- ddca_set_thread_description()
- ddca_append_thread_description()
- ddca_get_thread_desription() 

Remove previously deprecated functions: 
- ddca_open_display(). Use ddca_open_display2().
- ddca_create_display_ref(). Use ddca_get_display_ref()
- ddca_free_display_ref(). Had become a NO-OP. All display references are persistent

Symbols for functions and enums that had previously been removed from ddcutil_c_api.h are no longer exported.

Options that apply only to libddcutil (Specified in the ddcutil configuration file or passed to ddca_init())

- Option ***--profile-api***. Applies only to **libddcutil**.  Statistics
  for API functions that perform display IO are collected and subsequently
  reported when the library is terminated.  

- Option ***--trcapi***. Trace the call stack for a specified API function.

#### Building ddcutil

- configure options --enable-syslog/--disable-syslog have been eliminated.   
  Use runtime option ***-syslog NEVER*** to disable all writes to the system log.
- Use of shared library **libkmod** eliminated.
- Shared library **libjansson** is now required


## [1.4.2] 2023-02-17

### Added 

- **ddcutil** installation installs files /usr/lib/modules-load.d/ddcutil.conf
  and /usr/lib/modules-load.d#libddcutil.conf to ensure that kernel module 
  i2c-dev is loaded at boot time if it is not built into the kernel. There are
  two files so that when split up into distribution packages, each of the 
  command line **ddcutil** package and the shared library **libddcutil** 
  package installs a file.  

## [1.4.1] 2023-01-16

### Fixed
- The default sleep-multipler value was 0, instead of 1. This resulted in failure of
  most DDC/CI operations, including display detection.
  
## [1.4.0] 2023-01-04

### Added
- **ddcutil** installation installs file /usr/lib/udev/rules.c/60-ddcutil.rules.
  This udev rule uses tag uaccess to  give the logged on user read/write access
  to /dev/i2c devices associated with video adapters. Configuring use of group 
  i2c is no longer necessary.
- configure options ***--enable-syslog*** and ***--disable-syslog*** control 
  whether messages are written to the system log.  The default is enabled.

### Changed
- The ability to use the write()/read() interface of i2c-dev has been restored.
  It is needed to work around a bug in the proprietary Nvidia driver.  By
  default,  ioctl() interface is used for all drivers.  If the Nvidia bug is 
  detected, the write()/read() interface is used instead. Command line options 
  ***--use-file-io*** and ***--use-ioctl-io*** affect this default behavior. 
  When i2c-dev's file io interface is used, option ***--force-slave-address***
  is again meaningful.
- Option ***--sleep-multiplier*** and API functions **ddca_set_sleep_multiplier_value()**, 
  **ddca_set_default_sleep_multiplier_value()** now accept 0 as a valid 
  argument.
- The ddcutil command parser reports an error if a display selection option 
  (e.g. ***--bus***) is given on a command to which it does not apply.
- Write additional error and information messages to the system log.
- Eliminate message "Is DDC/CI enabled in the monitor's on-screen display?"
  It's rarely the cause of communication failures.

### Fixed
- Warn of a possibly invalid DRM connector name in **detect** output if 
  monitors with identical EDIDs are used with the proprietary nvidia driver.
- Handle /dev/i2c device names with a double hyphen, e.g. /dev/i2c--3. 
- Better libddcutil handling of configuration file errors.  Do not abort
  initialization in case of errors.
- Fix interpretation of digital display type bits for EDID version 1.4
- Miscellaneous segfaults.

## [1.3.2] 2022-09-04

### Changed
- Modify tarball creation to eliminate garbage and otherwise unneeded files.

## [1.3.0] 2022-07-19

### Added
- Command **detect**: 
  - Issue warning for monitors for which **ddcutil** should not be used to
    change settings.
    - Currently only entry is Xaomi model "Mi Monitor"
- Debug messages.  Environment variables DDCUTIL_DEBUG_PARSE, 
  DDCUTIL_DEBUG_MAIN, DDCUTIL_DEBUG_LIBINIT can be set to enable trace messages
  in command line **ddcutil** or shared library **libddcutil.so** during 
  initialization and before command options.

### Changed
- Option ***--force-slave-address*** no longer has any effect. The dev-i2c 
  ioctl() interface is now used exclusively instead of write() and read()
  calls for writing to and reading from the I2C bus. As a result, 
  ioctl(SLAVE_ADDRESS), which has been the source of EBUSY errors from driver
  i2-dev, is no longer used. In principle, EBUSY errors are still possible from
  within individual video drivers, but this has never been observed.
- Sleeps immediately after opening a /dev/i2c device and after completion of a
  read operation are completely eliminated. The sleep-suppression related 
  uptions, ***--sleep-less***, ***--less-sleep, ***--enable-sleep-suppression***,
  and ***--disable-sleep-suppression*** no longer have any effect.
- Option ***--dca***: The Dynamic Sleep Adjustment algorithm was rewritten to 
  more sensibly increment sleep times after before each retry. 
- Commands **getvcp** and **vcpinfo**: 
  - Allow specification of multiple feature codes, for example 
    ***ddcutil getvcp 10 12*** , ***ddcutil vcpinfo 14 16 18 1a***
- Command **detect**: 
  - Option ***--verbose*** produces addtional information: 
   - The product code is reported in hex as well as decimal
   - The EDID source field is set to **I2C** in the normal case where the EDID
     is read directly from slave address X50.  Alternative values include 
     **USB**, **X11**, and **SYSFS**.
- Command **environment**:
   - Scanning of /sys by option ***--verbose*** has been improved.
   - Add msg re SYSENV_QUICK_TEST environment variable
- Command **interrogate**: 
   - Set --disable-capabilities-cache
- More user friendly messages at startup regarding /dev/i2c buses that cannont
  be opened.  If the problem is inadequate permissions (EACCES), the user is 
  directed to www.ddcutil.com/permissions.
- Better handle malformed EDIDs
  - Trailing blanks on model and serial number are stripped.  This affects 
    commands **detect --terse**, **loadvcp** and **dumpvcp**, and also the 
    file names of user defined features.
- Option ***--stats***: 
  - I2C ioctl() calls for reading and writing are now reported as type IE_IOCTL_WRITE
    and IE_IOCTL_READ rather than IE_OTHER
  - IE_WRITE_READ stats are no longer reported, as they are redundant
- Source code has been extensively cleaned up. In particular, directory **adl**
  containing code for the old proprietary AMD video driver, has been removed.
- Building ddcutil:
  - Library **libi2c.so** is no longer linked.  It was needed only for some
    experimental code.
libddcutil: 
- ddca_get_display_refs(), ddca_get_display_info_list2(): 
  - Open errors can be retrieved using ddca_get_error_info(). 
    Note that the API calls still succeed.
  - Deprecated API functions have no effect:
    - ddca_enable_force_slave_address(), ddca_is_force_slave_address_enabled()

### Fixed
- The sleep multiplier value was not respected for new API threads.
- User Defined Features: Keyword **NC** set the incorrect flag in a feature
  descriptor.
- Option **--dsa**: Fix adjustment factor calculation due to incorrect variable
  type.
- Fixed a segfault that occurred at **ddcui** startup.  The fault was in a 
  trace message for function ddc_start_watch_displays() which watches for
  displays that are added or removed.
- Fixed a segfault in **ddcutil** initialization because of unexpected
  contents in sysfs.
- Do not use glib function g_byte_array_steal(), which requires glib 2.60.
  ddcutil requires only glib 2.40. 
- Miscellaneous memory leaks
- Double count I2C writes in stats. 

## [1.2.2] 2022-01-22

### Added
- API function ddca_enable_force_slave_address()
- API function ddca_is_force_slave_address_enabled()

### Changed
- Improve handling of and messages regarding DDC communication failures with 
  errno EBUSY. In particular, this error occurs when driver ddcci is loaded.
  - Command **detect**: If DDC communication fails with error EBUSY, report the
    display as "Busy" instead of "Invalid" and suggest use of option 
    ***--force-slave-address***.
  - Command **environment**: Suggest use of option ***--force-slave-address*** 
    if driver ddcci is detected.
  - Messages re EBUSY errors are always written to the system log.
- Command **detect**:
  - Do not report the EDID source unless there is a value to show.
    This value is set only for USB connected monitors.
  - Show extended output based on option ***--verbose***, not undocumented
    option ***--very-verbose***.
  - Report color bit depth if EDID version >= 1.4
- Command **environment**: Simplify the exploration of sysfs.
- API changes:
  - Field latest_sl_values in struct DDCA_Feature_Metadata struct is no 
    longer set,
  - API function ddca_report_display_info(): include binary serial number
- Building and porting:
  - When building ddcutil, allow for building a static library if **configure** 
    option ***--enable-static*** is set. Linux distributions frown on packaging 
    static libraries, but if a user wants to build it who am I to judge. 
    By default, static libraries are not built,
  - Replace use of Linux specific function **__assert_fail()** with **exit()**
    in traced assertions.  **__assert_fail** is used in the Linux implementation
    of **assert()**, but is not in the C specification.  This can present a 
    problem in porting ddcutil. 
- Code cleanup:
  - Delete incomplete, experimental code for asynhronous feature access, 
    including files src/ddc/ddc_async.c/h. 
  - Remove unused files src/util/output_sink.c/h.

### Fixed
- Only write Starting/Terminating messages to the system log if option 
  ***--syslog*** is specified.
- Avoid compilation warnings when assert() statments are disabled (NDEBUG is
  defined).
- Fixed a segfault in the debug/trace code of ddca_get_display_refs()
- Memory leaks.

## [1.2.1] 2021-11-15

### Added
- Option ***--syslog***: Send trace and debug messages to the system log in
  addition to the trace location.
- Option ***--wall-timestamp***, ***--wts***: Prefix trace and debug messages
  with the current wall time.
- Option ***--settings***: Report option settings in effect.

### Changed
- Details of current settings are no longer reported by every command invocation
  when option ***--verbose*** is specified.  Use option ***--settings*** to 
  control option reporting.
- Removed sample program demo_watch_displays.

### Fixed
- Numerous memory leaks, in particular ones triggered by ddca_redetect_displays(). 
- Build failure if configure option ***--enable-x11=no*** was specified.
- API functions ddc_open_display(),ddc_open_display2() now always return 
  DDCRC_ALREADY_OPEN if the the display is already open in the current thread.
  Previously an assert() failure would occur under certain circumstances.
- Options ***--disable-capabilities-cache***, ***--disable-udf*** not respected
- Proof of concept code that watches for display hotplug events 

## [1.2.0] 2021-09-28

### Added
- libddcutil log file
- libddcuti and ddcutil write critical events to syslog
- API function ddca_add_trace_group()
- API function ddca_extended_version_string()
- API function ddca_redetect_displays()
- API function ddca_get_display_refs()
- API function ddca_get_display_info()
- API function ddca_free_display_info()
- Macro DDCUTIL_VSUFFIX

### Changed
- If possible, command **ddcutil environment --verbose** calls **get-edid|parse-edid** 
  as an additional EDID check.
- Additional validation of DDCA_Display_Ref and DDCA_Display_Handle arguments to API functions
- Improved tracing of assert() failures
- --enable-capabilities-cache is now the default
- libddcutil name is now libddcutil.so.4.1.0
- Command **detect**: improved analysis of /sys
- Command **detect**: ***--verbose*** option reports raw EDID
- Option ***--help*** does not report undocumented option ***--very-verbose***.

### Fixed
- Incorrect assembly of sysfs path definitions in **ddcutil environment --verbose** 
- ddcutil diagnostics were not finding module i2c-dev if the system (e.g. NixOS) 
  used a non-standard location for the modules directory (Issue #178). The checks 
  have been rewritten to use libkmod.
- Eliminate repeated messages from the experimental display hotplug detection code
  if no /sys/class/drm/cardN devices exist. (libddcutil)

## [1.1.0] 2021-04-05

For details, see [ddcutil Release Notes](https://www.ddcutil.com/release_notes).

### Added
- Configuration file **ddcutilrc**, located on the XDG config path.   
- Cache monitor capabilities strings to improve performance of the **capabilities** command.  
  Controlled by options ***--enable-capabilities-cache***, ***--disable-capabilities-cache***.
- Workarounds for problems in DRM video drivers (e.g. i915, AMDGPU) when displays are connected to 
  a docking station. The same monitor can appear as two different /dev/i2c devices,
  but only one supports DDC/CI.  If possible these are reported as a "Phantom Display" instead 
  of as "Invalid Display". Also, try to work around problems reading the EDID on these 
  monitors, which  can cause the monitor to not be detected.
- Option ***--edid-read-size 128*** or ***--edid-read-size 256*** forces **ddcutil** to request
  that number of bytes when reading the EDID, which can occasionally allow the EDID to
  be read successfully. 
- Issue warning at startup if driver i2c-dev is neither loaded nor built into the kernel.

### Changed
- By default, files generated by **dumpvcp** are saved in the XDG_DATA_HOME directory.
- **environment --verbose** has more detailed reporting of relevant sections of /sys.
- Additional information on **detect --verbose**.
- Additional functions are traceable using option ***--trcfunc***
- User defined features are enabled by default.

### Fixed
- Regard IO operations setting errno EBUSY as recoverable, suggest use of option 
  ***--force-slave-address***.  (EBUSY can occur when ddcontrol's ddcci driver 
  is loaded.)
- Fix build failure when configure option ***--disable-usb*** is combined with 
  ***--enable-envcmds***.
- On AMD Navi2 variants, e.g. RX 6000 series, **ddcutil** display detection put
 the GPU into an inconsistent state when probing a SMU I2C bus exposed by the GPU. 
 This change ensures that **ddcutil** does not attempt to probe such buses. 

