/** @file main.c
 *
 *  ddcutil standalone application mainline
 */

// Copyright (C) 2014-2021 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/** \cond */
#include <config.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <glib-2.0/glib.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// #include <wordexp.h>

#include "util/data_structures.h"
#include "util/config_file.h"
#include "util/ddcutil_config_file.h"
#include "util/error_info.h"
#include "util/failsim.h"
#include "util/file_util.h"
#include "util/glib_string_util.h"
#include "util/linux_util.h"
#include "util/report_util.h"
#include "util/string_util.h"
#include "util/subprocess_util.h"
#include "util/sysfs_i2c_util.h"
#include "util/sysfs_util.h"
#include "util/xdg_util.h"
/** \endcond */

#include "public/ddcutil_types.h"

#include "base/base_init.h"
#include "base/build_info.h"
#include "base/core.h"
#include "base/ddc_errno.h"
#include "base/ddc_packets.h"
#include "base/displays.h"
#include "base/dynamic_sleep.h"
#include "base/linux_errno.h"
#include "base/monitor_model_key.h"
#include "base/parms.h"
#include "base/rtti.h"
#include "base/sleep.h"
#include "base/status_code_mgt.h"
#include "base/thread_retry_data.h"
#include "base/thread_sleep_data.h"
#include "base/tuned_sleep.h"

#include "ddc/common_init.h"

#include "vcp/parse_capabilities.h"
#include "vcp/persistent_capabilities.h"
#include "vcp/vcp_feature_codes.h"

#include "dynvcp/dyn_feature_files.h"
#include "dynvcp/dyn_parsed_capabilities.h"

#include "i2c/i2c_bus_core.h"
#include "i2c/i2c_strategy_dispatcher.h"

#ifdef USE_USB
#include "usb/usb_displays.h"
#endif

#include "ddc/ddc_displays.h"
#include "ddc/ddc_multi_part_io.h"
#include "ddc/ddc_output.h"
#include "ddc/ddc_packet_io.h"
#include "ddc/ddc_read_capabilities.h"
#include "ddc/ddc_services.h"
#include "ddc/ddc_try_stats.h"
#include "ddc/ddc_vcp_version.h"
#include "ddc/ddc_vcp.h"

#include "cmdline/cmd_parser_aux.h"    // for parse_feature_id_or_subset(), should it be elsewhere?
#include "cmdline/cmd_parser.h"
#include "cmdline/parsed_cmd.h"

#include "test/testcases.h"

#include "app_ddcutil/app_capabilities.h"
#include "app_ddcutil/app_dynamic_features.h"
#include "app_ddcutil/app_dumpload.h"
#include "app_ddcutil/app_experimental.h"
#include "app_ddcutil/app_probe.h"
#include "app_ddcutil/app_getvcp.h"
#include "app_ddcutil/app_setvcp.h"
#include "app_ddcutil/app_vcpinfo.h"
#ifdef INCLUDE_TESTCASES
#include "app_ddcutil/app_testcases.h"
#endif

#include "app_sysenv/query_sysenv.h"
#ifdef USE_USB
#include "app_sysenv/query_sysenv_usb.h"
#endif


// Default trace class for this file
static DDCA_Trace_Group TRACE_GROUP = DDCA_TRC_TOP;

static void init_rtti();


//
// Report core settings and command line options
//

#define REPORT_FLAG_OPTION(_flagno, _action) \
rpt_vstring(depth+1, "Utility option --f"#_flagno" %s %s",   \
     (parsed_cmd->flags & CMD_FLAG_F##_flagno ) ? "enabled: " : "disabled:", _action)
static void
report_experimental_options(Parsed_Cmd * parsed_cmd, int depth)
{
      rpt_label(depth, "Experimental Options:");
      REPORT_FLAG_OPTION(1, "EDID read uses I2C layer");
      REPORT_FLAG_OPTION(2, "Unused");    // was Filter phantom displays
      REPORT_FLAG_OPTION(3, "Unused");
      REPORT_FLAG_OPTION(4, "Read strategy tests");
      REPORT_FLAG_OPTION(5, "Unused");
      REPORT_FLAG_OPTION(6, "Force I2c bus");

      rpt_vstring(depth+1, "Utility option --i1 = %d:     Unused", parsed_cmd->i1);
      rpt_nl();

 }
#undef REPORT_FLAG_OPTION


static void
report_performance_options(int depth)
{
      int d1 = depth+1;
      rpt_label(depth, "Performance and Retry Options:");
      rpt_vstring(d1, "Deferred sleep enabled:                      %s", sbool( is_deferred_sleep_enabled() ) );
      rpt_vstring(d1, "Sleep suppression (reduced sleeps) enabled:  %s", sbool( is_sleep_suppression_enabled() ) );
      bool dsa_enabled =  tsd_get_dsa_enabled_default();
      rpt_vstring(d1, "Dynamic sleep adjustment enabled:            %s", sbool(dsa_enabled) );
      if ( dsa_enabled )
        rpt_vstring(d1, "Sleep multiplier factor:                %5.2f", tsd_get_sleep_multiplier_factor() );
      rpt_nl();
}


static void
report_all_options(Parsed_Cmd * parsed_cmd, int depth)
{
    if (parsed_cmd->output_level >= DDCA_OL_VV)
       report_build_options(depth);

    show_reporting();  // uses fout()

    rpt_vstring( depth, "%.*s%-*s%s",
              0,"",
              28, "Force I2C slave address:",
              sbool(i2c_force_slave_addr_flag));
    rpt_vstring( depth, "%.*s%-*s%s",
              0,"",
              28, "User defined features:",
              (enable_dynamic_features) ? "enabled" : "disabled" );  // "Enable user defined features" is too long a title
    rpt_nl();

    report_performance_options(depth);

    if (parsed_cmd->output_level >= DDCA_OL_VV)
       report_experimental_options(parsed_cmd, depth);
}


//
// Initialization functions called only once but factored out of main() to clarify mainline
//

#ifdef ENABLE_ENVCMDS
static void
reset_stats() {
   ddc_reset_stats_main();
}
#endif


static bool
validate_environment()
{
   bool debug = false;
   DBGMSF(debug, "Starting");

   bool ok = false;
#ifdef TARGET_LINUX
   if (is_module_loaded_using_sysfs("i2c_dev")) {
      ok = true;
   }
   else {
#ifdef USE_CONFIG_FILE
      char * parm_name = "CONFIG_I2C_CHARDEV";
      int  value_buf_size = 40;
      char value_buffer[value_buf_size];
      int config_rc = get_kernel_config_parm(parm_name, value_buffer, value_buf_size);
      DBGMSF(debug, "config_rc = %d", config_rc);
      if (config_rc < 0) {
         fprintf(stderr, "Unable to read read kernel configuration file: errno=%d, %s\n", -config_rc, strerror(-config_rc));
         // fprintf(stderr, "Module i2c-dev is not loaded and ddcutil can't determine if it is built into the kernel\n");
         ok = false;
      }
      else if (config_rc == 0) {
         fprintf(stderr,
               "Configuration parameter %s not found in kernel configuration file\n",
               parm_name);
         // fprintf(stderr, "Module i2c-dev is not loaded and ddcutil can't determine if it is built into the kernel\n");
         ok = false;
      }
      else {
         DBGMSF(debug, "get_kernel_config_parm(%s, ...) returned |%s|", parm_name, value_buffer);
         if (!streq(value_buffer, "y")) {
            fprintf(stderr, "Module i2c-dev is not loaded and the kernel configuration"
                            " file indicates is not built into the kernel.\n");
            ok = false;
         }
         else
            ok = true;
      }
      // config_rc = -1;   // force failure for testing
      if (config_rc < 0) {   // if couldn't read config file
#endif
         int modules_rc = is_module_builtin("i2c-dev");
         // consider calling is_module_loadable() if not built in
         if (modules_rc < 0) {
            fprintf(stderr, "Unable to read modules.builtin\n");
            fprintf(stderr, "Module i2c-dev is not loaded and ddcutil can't determine"
                            " if it is built into the kernel\n");
            ok = true;  // make this just a warning, we'll fail later if not in kernel
         }
         else if (modules_rc == 0) {
            ok = false;
            fprintf(stderr, "Module i2c-dev is not loaded and not built into the kernel.\n");
         }
         else {
            ok = true;
         }
      }
      if (!ok) {
         fprintf(stderr, "ddcutil requires module i2c-dev\n");
         // DBGMSF(debug, "Forcing ok = true");
         // ok = true;  // make it just a warning in case we're wrong
      }
#ifdef USE_CONFIG_FILE
  }
#endif
#else
   ok = true;
#endif

   DBGMSF(debug, "Done. Returning: %s", sbool(ok));
   return ok;
}


bool init_experimental_options(Parsed_Cmd* parsed_cmd)
{
   if (parsed_cmd->flags & CMD_FLAG_F1) {
      fprintf(stdout, "EDID reads will use normal I2C calls\n");
      EDID_Read_Uses_I2C_Layer = true;
   }

   // if (parsed_cmd->flags & CMD_FLAG_F2) {
   //    fprintf(stdout, "Filter phantom displays\n");
   //   check_phantom_displays = true;    // extern in ddc_displays.h
   // }

   // HACK FOR TESTING
   if (parsed_cmd->flags & CMD_FLAG_F6) {
      fprintf(stdout, "Setting i2c_force_bus\n");
      if ( !(parsed_cmd->pdid) || parsed_cmd->pdid->id_type != DISP_ID_BUSNO) {
         fprintf(stdout, "bus number required, use --busno\n");
         return false;
      }
      i2c_force_bus = true;
   }
   return true;
}



/** Master initialization function
 *
 *   \param  parsed_cmd  parsed command line
 *   \return ok if successful, false if error
 */
static bool
master_initializer(Parsed_Cmd * parsed_cmd, char * default_options) {
   bool ok = false;

submaster_initializer(parsed_cmd);

#ifdef ENABLE_ENVCMDS
   if (parsed_cmd->cmd_id != CMDID_ENVIRONMENT) {
      // will be reported by the environment command
#endif
      if (!validate_environment())
         goto bye;
#ifdef ENABLE_ENVCMDS
   }
#endif

#ifdef ENABLE_ENVCMDS
    init_sysenv();
#endif

    if (!init_experimental_options(parsed_cmd))
       goto bye;

    if (parsed_cmd->output_level >= DDCA_OL_VERBOSE) {
       show_ddcutil_version();
       rpt_vstring(0, "%.*s%-*s%s",
                 0,"",
                 28, "Config file options:", default_options);
       report_all_options(parsed_cmd, 0);
    }

   ok = true;

bye:
   return ok;
}

#ifdef ENABLE_ENVCMDS

/** Execute the INTERROGATE command
 *
 *  \param parsed_cmd  parsed command line
 *
 *  \remark
 *  This command is in main.c instead of in a separate app_XXX.c file
 *  because it executes multiple commands.
 */
void interrogate(Parsed_Cmd * parsed_cmd)
{
   bool debug = false;
   DBGTRC(debug, TRACE_GROUP, "Processing command INTERROGATE...");
   dup2(1,2);   // redirect stderr to stdout
   // set_ferr(fout);    // ensure that all messages are collected - made unnecessary by dup2()
   f0printf(fout(), "Setting output level verbose...\n");
   set_output_level(DDCA_OL_VERBOSE);
   f0printf(fout(), "Setting maximum retries...\n");
   f0printf(fout(), "Forcing --stats...\n");
   parsed_cmd->stats_types = DDCA_STATS_ALL;
   f0printf(fout(), "Forcing --force-slave-address..\n");
   i2c_force_slave_addr_flag = true;
   f0printf(fout(), "This command will take a while to run...\n\n");
   try_data_set_maxtries2(MULTI_PART_READ_OP, MAX_MAX_TRIES);
   try_data_set_maxtries2(MULTI_PART_WRITE_OP, MAX_MAX_TRIES);

   ddc_ensure_displays_detected();    // *** ???
   DBGTRC(debug, TRACE_GROUP, "display detection complete");

   query_sysenv();
#ifdef USE_USB
   // 7/2017: disable, USB attached monitors are rare, and this just
   // clutters the output
   f0printf(fout(), "\nSkipping USB environment exploration.\n");
   f0printf(fout(), "Issue command \"ddcutil usbenvironment --verbose\" if there are any USB attached monitors.\n");
   // query_usbenv();
#endif
   f0printf(fout(), "\nStatistics for environment exploration:\n");
   ddc_report_stats_main(DDCA_STATS_ALL, parsed_cmd->flags & CMD_FLAG_PER_THREAD_STATS, 0);
   reset_stats();

   f0printf(fout(), "\n*** Detected Displays ***\n");
   /* int display_ct =  */ ddc_report_displays(
                              true,   // include_invalid_displays
                              0);      // logical depth
   // printf("Detected: %d displays\n", display_ct);   // not needed
   f0printf(fout(), "\nStatistics for display detection:\n");
   ddc_report_stats_main(DDCA_STATS_ALL, parsed_cmd->flags & CMD_FLAG_PER_THREAD_STATS, 0);
   reset_stats();

   f0printf(fout(), "Setting output level normal  Table features will be skipped...\n");
   set_output_level(DDCA_OL_NORMAL);

   tsd_dsa_enable_globally(parsed_cmd->flags & CMD_FLAG_DSA);   // should this apply to INTERROGATE?
   GPtrArray * all_displays = ddc_get_all_displays();
   for (int ndx=0; ndx < all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      assert( memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0);
      if (dref->dispno < 0) {
         f0printf(fout(), "\nSkipping invalid display on %s\n", dref_short_name_t(dref));
      }
      else {
         f0printf(fout(), "\nProbing display %d\n", dref->dispno);
         app_probe_display_by_dref(dref);
         f0printf(fout(), "\nStatistics for probe of display %d:\n", dref->dispno);
         ddc_report_stats_main(DDCA_STATS_ALL, parsed_cmd->flags & CMD_FLAG_PER_THREAD_STATS, 0);
      }
      reset_stats();
   }
   f0printf(fout(), "\nDisplay scanning complete.\n");
   DBGTRC(debug, TRACE_GROUP, "Done");
}
#endif


static void
ensure_vcp_version_set(Display_Handle * dh)
{
   bool debug = false;
   DBGMSF(debug, "Starting. dh=%s", dh_repr(dh));
   DDCA_MCCS_Version_Spec vspec = get_vcp_version_by_dh(dh);
   if (vspec.major < 2 && get_output_level() >= DDCA_OL_NORMAL) {
      f0printf(stdout, "VCP (aka MCCS) version for display is undetected or less than 2.0. "
            "Output may not be accurate.\n");
   }
   DBGMSF(debug, "Done");
}


typedef enum {
   DISPLAY_ID_REQUIRED,
   DISPLAY_ID_USE_DEFAULT,
   DISPLAY_ID_OPTIONAL
} Displayid_Requirement;


const char *
displayid_requirement_name(Displayid_Requirement id) {
   char * result = NULL;
   switch (id) {
   case DISPLAY_ID_REQUIRED:    result = "DISPLAY_ID_REQUIRED";     break;
   case DISPLAY_ID_USE_DEFAULT: result = "DISPLAY_ID_USE_DEFAULT";  break;
   case DISPLAY_ID_OPTIONAL:    result = "DISPLAY_ID_OPTIONAL";     break;
   }
   return result;
}


/** Returns a display reference for the display specified on the command line,
 *  or, if a display is not optional for the command, a reference to the
 *  default display (--display 1).
 *
 *  \param  parsed_cmd  parsed command line
 *  \param  displayid_required how to handle no display specified on command line
 *  \param  dref_loc  where to return display reference
 *  \retval DDCRC_OK
 *  \retval DDCRC_INVALID_DISPLAY
 */
Status_Errno_DDC
find_dref(
      Parsed_Cmd * parsed_cmd,
      Displayid_Requirement displayid_required,
      Display_Ref ** dref_loc)
{
   bool debug = false;
   DBGTRC(debug, TRACE_GROUP, "did: %s, set_default_display: %s",
                                    did_repr(parsed_cmd->pdid),
                                    displayid_requirement_name(displayid_required));
   FILE * outf = fout();
   Status_Errno_DDC final_result = DDCRC_OK;
   Display_Ref * dref = NULL;
   Call_Options callopts = CALLOPT_ERR_MSG;        // emit error messages
   if (parsed_cmd->flags & CMD_FLAG_FORCE)
      callopts |= CALLOPT_FORCE;

   Display_Identifier * did_work = parsed_cmd->pdid;
   if (did_work && did_work->id_type == DISP_ID_BUSNO) {
      DBGTRC(debug, DDCA_TRC_NONE, "Special handling for explicit --busno");
      int busno = did_work->busno;
      // is this really a monitor?
      I2C_Bus_Info * businfo = i2c_detect_single_bus(busno);
      if (businfo) {
         if (businfo->flags & I2C_BUS_ADDR_0X50)  {
            dref = create_bus_display_ref(busno);
            dref->dispno = -1;     // should use some other value for unassigned vs invalid
            dref->pedid = businfo->edid;    // needed?
            dref->mmid  = monitor_model_key_new(
                             dref->pedid->mfg_id,
                             dref->pedid->model_name,
                             dref->pedid->product_code);

            // dref->pedid = i2c_get_parsed_edid_by_busno(did_work->busno);
            dref->detail = businfo;
            dref->flags |= DREF_DDC_IS_MONITOR_CHECKED;
            dref->flags |= DREF_DDC_IS_MONITOR;
            dref->flags |= DREF_TRANSIENT;
            if (!ddc_initial_checks_by_dref(dref)) {
               f0printf(outf, "DDC communication failed for monitor on bus /dev/i2c-%d\n", busno);
               free_display_ref(dref);
               i2c_free_bus_info(businfo);
               dref = NULL;
               final_result = DDCRC_INVALID_DISPLAY;
            }
            else {
               DBGTRC(debug, TRACE_GROUP, "Synthetic Display_Ref");
               final_result = DDCRC_OK;
            }
         }  // has edid
         else {   // no EDID found
            f0printf(fout(), "No monitor detected on bus /dev/i2c-%d\n", busno);
            i2c_free_bus_info(businfo);
            final_result = DDCRC_INVALID_DISPLAY;
         }
      }    // businfo allocated
      else {
         f0printf(fout(), "Bus /dev/i2c-%d not found\n", busno);
         final_result = DDCRC_INVALID_DISPLAY;
      }
   }       // DISP_ID_BUSNO
   else {
      if (!did_work && displayid_required == DISPLAY_ID_OPTIONAL) {
         DBGTRC(debug, DDCA_TRC_NONE, "No monitor specified, none required for command");
         dref = NULL;
         final_result = DDCRC_OK;
      }
      else {
         DBGTRC(debug, DDCA_TRC_NONE, "No monitor specified, treat as  --display 1");
         bool temporary_did_work = false;
         if (!did_work) {
            did_work = create_dispno_display_identifier(1);   // default monitor
            temporary_did_work = true;
         }
         // assert(did_work);
         DBGTRC(debug, TRACE_GROUP, "Detecting displays...");
         ddc_ensure_displays_detected();
         DBGTRC(debug, TRACE_GROUP, "display detection complete");
         dref = get_display_ref_for_display_identifier(did_work, callopts);
         if (temporary_did_work)
            free_display_identifier(did_work);
         final_result = (dref) ? DDCRC_OK : DDCRC_INVALID_DISPLAY;
      }
   }  // !DISP_ID_BUSNO

   *dref_loc = dref;
   DBGTRC(debug, TRACE_GROUP,
                 "Done. *dref_loc = %p -> %s , returning %s",
                 *dref_loc,
                 dref_repr_t(*dref_loc),
                 psc_desc(final_result));
   return final_result;
}


/** Execute commands that either require a display or for which a display is optional.
 *  If a display is required, it has been opened and its display handle is passed
 *  as an argument.
 *
 *  \param parsed_cmd  parsed command line
 *  \param dh          display handle, if NULL no display was specified on the
 *                     command line and the command does not require a display
 *  \retval EXIT_SUCCESS
 *  \retval EXIT_FAILURE
 */
int
execute_cmd_with_optional_display_handle(
      Parsed_Cmd *     parsed_cmd,
      Display_Handle * dh)
{
   bool debug = false;
   int main_rc =EXIT_SUCCESS;

   if (dh) {
      if (!vcp_version_eq(parsed_cmd->mccs_vspec, DDCA_VSPEC_UNKNOWN)) {
         DBGTRC(debug, TRACE_GROUP, "Forcing mccs_vspec=%d.%d",
                            parsed_cmd->mccs_vspec.major, parsed_cmd->mccs_vspec.minor);
         dh->dref->vcp_version_cmdline = parsed_cmd->mccs_vspec;
      }
   }

   DBGTRC(debug, TRACE_GROUP, "%s", cmdid_name(parsed_cmd->cmd_id));
   switch(parsed_cmd->cmd_id) {

   case CMDID_LOADVCP:
      {
         // check_dynamic_features();
         // ensure_vcp_version_set();

         tsd_dsa_enable(parsed_cmd->flags & CMD_FLAG_DSA);
         // loadvcp will search monitors to find the one matching the
         // identifiers in the record
         ddc_ensure_displays_detected();
         bool loadvcp_ok = loadvcp_by_file(parsed_cmd->args[0], dh);
         main_rc = (loadvcp_ok) ? EXIT_SUCCESS : EXIT_FAILURE;
         break;
      }

   case CMDID_CAPABILITIES:
      {
         assert(dh);
         check_dynamic_features(dh->dref);
         ensure_vcp_version_set(dh);

         DDCA_Status ddcrc = app_capabilities(dh);
         main_rc = (ddcrc==0) ? EXIT_SUCCESS : EXIT_FAILURE;
         break;
      }

   case CMDID_GETVCP:
      {
         assert(dh);
         check_dynamic_features(dh->dref);
         ensure_vcp_version_set(dh);

         Public_Status_Code psc = app_show_feature_set_values_by_dh(dh, parsed_cmd);
         main_rc = (psc==0) ? EXIT_SUCCESS : EXIT_FAILURE;
      }
      break;

   case CMDID_SETVCP:
      {
         assert(dh);
         check_dynamic_features(dh->dref);
         ensure_vcp_version_set(dh);

         bool ok = app_setvcp(parsed_cmd, dh);
         main_rc = (ok) ? EXIT_SUCCESS : EXIT_FAILURE;
      }
      break;

   case CMDID_SAVE_SETTINGS:
      assert(dh);
      if (parsed_cmd->argct != 0) {
         f0printf(fout(), "SCS command takes no arguments\n");
         main_rc = EXIT_FAILURE;
      }
      else if (dh->dref->io_path.io_mode == DDCA_IO_USB) {
         f0printf(fout(), "SCS command is not supported for USB devices\n");
         main_rc = EXIT_FAILURE;
      }
      else {
         main_rc = EXIT_SUCCESS;
         Error_Info * ddc_excp = ddc_save_current_settings(dh);
         if (ddc_excp)  {
            f0printf(fout(), "Save current settings failed. rc=%s\n", psc_desc(ddc_excp->status_code));
            if (ddc_excp->status_code == DDCRC_RETRIES)
               f0printf(fout(), "    Try errors: %s", errinfo_causes_string(ddc_excp) );
            errinfo_report(ddc_excp, 0);   // ** ALTERNATIVE **/
            errinfo_free(ddc_excp);
            // ERRINFO_FREE_WITH_REPORT(ddc_excp, report_exceptions);
            main_rc = EXIT_FAILURE;
         }
      }
      break;

   case CMDID_DUMPVCP:
      {
         assert(dh);
         // MCCS vspec can affect whether a feature is NC or TABLE
         check_dynamic_features(dh->dref);
         ensure_vcp_version_set(dh);

         Public_Status_Code psc =
               dumpvcp_as_file(dh, (parsed_cmd->argct > 0)
                                      ? parsed_cmd->args[0]
                                      : NULL );
         main_rc = (psc==0) ? EXIT_SUCCESS : EXIT_FAILURE;
         break;
      }

   case CMDID_READCHANGES:
      assert(dh);
      check_dynamic_features(dh->dref);
      ensure_vcp_version_set(dh);

      app_read_changes_forever(dh, parsed_cmd->flags & CMD_FLAG_X52_NO_FIFO);     // only returns if fatal error
      main_rc = EXIT_FAILURE;
      break;

   case CMDID_PROBE:
      assert(dh);
      check_dynamic_features(dh->dref);
      ensure_vcp_version_set(dh);

      app_probe_display_by_dh(dh);
      main_rc = EXIT_SUCCESS;
      break;

   default:
      main_rc = EXIT_FAILURE;
      break;
   }    // switch

   return main_rc;
}


//
// Configuration file
//


#ifdef OLD

/** Combines the options from the ddcutil configuration file with the command line arguments,
 *  returning a new list of tokens.
 *
 *  \param  old_argc  argc as passed on the command line
 *  \param  old argv  argv as passed on the command line
 *  \param  new_argv_loc  where to return the address of the combined token list
 *  \param  detault_options_loc  where to return string of options obtained from ini file
 *  \return number of tokens in the combined list, -1 if errors
 *          reading the configuration file. n. it is not an error if the
 *          configuration file does not exist.  In that case 0 is returned.
 */
static
int apply_config_file(
      const char * ddcutil_application,     // "ddcutil", "ddcui"
      int      old_argc,
      char **  old_argv,
      char *** new_argv_loc,
      char**   default_options_loc)
{
   return full_arguments(
         "ddcutil",     // "ddcutil", "ddcui"
         old_argc,
         old_argv,
         new_argv_loc,
         default_options_loc);

}

#endif

//
// Mainline
//

/** **ddcutil** program mainline.
  *
  * @param argc   number of command line arguments
  * @param argv   pointer to array of argument strings
  *
  * @retval  EXIT_SUCCESS normal exit
  * @retval  EXIT_FAILURE an error occurred
  */
int
main(int argc, char *argv[]) {
   // FILE * fout = stdout;
   bool main_debug = false;
   int main_rc = EXIT_FAILURE;
   Parsed_Cmd * parsed_cmd = NULL;
   init_base_services();  // so tracing related modules are initialized

   char ** new_argv = NULL;
   char *  combined_config_file_options = NULL;
   int new_argc = full_arguments("ddcutil", argc, argv, &new_argv, &combined_config_file_options);
   if (new_argc < 0)
      goto bye;

   // DBGMSG("new_argc = %d", new_argc);
   // ntsa_show(new_argv);

   parsed_cmd = parse_command(new_argc, new_argv, MODE_DDCUTIL);
   if (!parsed_cmd) {
      goto bye;      // main_rc == EXIT_FAILURE
   }
   init_tracing(parsed_cmd);
   init_rtti();      // add entries for this file

   time_t cur_time = time(NULL);
   char * cur_time_s = asctime(localtime(&cur_time));
   if (cur_time_s[strlen(cur_time_s)-1] == 0x0a)
        cur_time_s[strlen(cur_time_s)-1] = 0;
   DBGTRC(parsed_cmd->traced_groups || parsed_cmd->traced_functions || parsed_cmd->traced_files,
          TRACE_GROUP,   /* redundant with parsed_cmd->traced_groups */
          "Starting ddcutil execution, %s",
          cur_time_s);

   bool ok = master_initializer(parsed_cmd, combined_config_file_options);
   free(combined_config_file_options);
   if (!ok)
      goto bye;

   // xdg_tests(); // for development

   // Initialization complete, rtti now contains entries for all traced functions
   // Check that any functions specified on --trcfunc are actually traced.
   // dbgrpt_rtti_func_name_table(0);
   if (parsed_cmd->traced_functions) {
      for (int ndx = 0; ndx < ntsa_length(parsed_cmd->traced_functions); ndx++) {
         char * func_name = parsed_cmd->traced_functions[ndx];
         // DBGMSG("Verifying: %s", func_name);
         if (!rtti_get_func_addr_by_name(func_name)) {
            rpt_vstring(0, "Traced function not found: %s", func_name);
            goto bye;
         }
      }
   }

   Call_Options callopts = CALLOPT_NONE;
   i2c_force_slave_addr_flag = parsed_cmd->flags & CMD_FLAG_FORCE_SLAVE_ADDR;
   if (parsed_cmd->flags & CMD_FLAG_FORCE)
      callopts |= CALLOPT_FORCE;

   main_rc = EXIT_SUCCESS;     // from now on assume success;
   DBGTRC(main_debug, TRACE_GROUP, "Initialization complete, process commands");

   if (parsed_cmd->cmd_id == CMDID_LISTVCP) {    // vestigial
      app_listvcp(stdout);
      main_rc = EXIT_SUCCESS;
   }

   else if (parsed_cmd->cmd_id == CMDID_VCPINFO) {
      bool vcpinfo_ok = app_vcpinfo(parsed_cmd);
      main_rc = (vcpinfo_ok) ? EXIT_SUCCESS : EXIT_FAILURE;
   }

#ifdef INCLUDE_TESTCASES
   else if (parsed_cmd->cmd_id == CMDID_LISTTESTS) {
      show_test_cases();
      main_rc = EXIT_SUCCESS;
   }
#endif

   // start of commands that actually access monitors

   else if (parsed_cmd->cmd_id == CMDID_DETECT) {
      DBGTRC(main_debug, TRACE_GROUP, "Detecting displays...");
      if ( parsed_cmd->flags & CMD_FLAG_F4) {
         test_display_detection_variants();
      }
      else {     // normal case
         ddc_ensure_displays_detected();
         ddc_report_displays(/*include_invalid_displays=*/ true, 0);
      }
      DBGTRC(main_debug, TRACE_GROUP, "Display detection complete");
      main_rc = EXIT_SUCCESS;
   }

#ifdef INCLUDE_TESTCASES
   else if (parsed_cmd->cmd_id == CMDID_TESTCASE) {
      bool ok = app_testcases(parsed_cmd);
      main_rc = (ok) ? EXIT_SUCCESS : EXIT_FAILURE;
   }
#endif


#ifdef ENABLE_ENVCMDS
   else if (parsed_cmd->cmd_id == CMDID_ENVIRONMENT) {
      DBGTRC(main_debug, TRACE_GROUP, "Processing command ENVIRONMENT...");
      dup2(1,2);   // redirect stderr to stdout
      query_sysenv();
      main_rc = EXIT_SUCCESS;
   }

   else if (parsed_cmd->cmd_id == CMDID_USBENV) {
#ifdef USE_USB
      DBGTRC(main_debug, TRACE_GROUP, "Processing command USBENV...");
      dup2(1,2);   // redirect stderr to stdout
      query_usbenv();
      main_rc = EXIT_SUCCESS;
#else
      f0printf(fout(), "ddcutil was not built with support for USB connected monitors\n");
      main_rc = EXIT_FAILURE;
#endif
   }
#endif

   else if (parsed_cmd->cmd_id == CMDID_CHKUSBMON) {
#ifdef USE_USB
      // DBGMSG("Processing command chkusbmon...\n");
      DBGTRC(main_debug, TRACE_GROUP, "Processing command CHKUSBMON...");
      bool is_monitor = check_usb_monitor( parsed_cmd->args[0] );
      main_rc = (is_monitor) ? EXIT_SUCCESS : EXIT_FAILURE;
#else
      PROGRAM_LOGIC_ERROR("ddcutil not built with USB support");
      main_rc = EXIT_FAILURE;
#endif
   }

#ifdef ENABLE_ENVCMDS
   else if (parsed_cmd->cmd_id == CMDID_INTERROGATE) {
      interrogate(parsed_cmd);
      main_rc = EXIT_SUCCESS;
   }
#endif

   // *** Commands that may require Display Identifier ***
   else {
      Display_Ref * dref = NULL;
      Status_Errno_DDC  rc =
      find_dref(parsed_cmd,
               (parsed_cmd->cmd_id == CMDID_LOADVCP) ? DISPLAY_ID_OPTIONAL : DISPLAY_ID_REQUIRED,
               &dref);
      if (rc != DDCRC_OK) {
         main_rc = EXIT_FAILURE;
      }
      else {
         Display_Handle * dh = NULL;
         if (dref) {
            DBGMSF(main_debug,
                   "mainline - display detection complete, about to call ddc_open_display() for dref" );
            Status_Errno_DDC ddcrc = ddc_open_display(dref, callopts |CALLOPT_ERR_MSG, &dh);
            ASSERT_IFF( (ddcrc==0), dh);
            if (!dh) {
               f0printf(ferr(), "Error %s opening display ref %s", psc_desc(ddcrc), dref_repr_t(dref));
               main_rc = EXIT_FAILURE;
            }
         }  // dref

         if (main_rc == EXIT_SUCCESS) {
            // affects all current threads and new threads
            tsd_dsa_enable_globally(parsed_cmd->flags & CMD_FLAG_DSA);
            main_rc = execute_cmd_with_optional_display_handle(parsed_cmd, dh);
         }

         if (dh)
               ddc_close_display(dh);
         if (dref && (dref->flags & DREF_TRANSIENT))
            free_display_ref(dref);
      }
   }

   if (parsed_cmd->stats_types != DDCA_STATS_NONE
#ifdef ENABLE_ENVCMDS
         && parsed_cmd->cmd_id != CMDID_INTERROGATE
#endif
      )
   {
      ddc_report_stats_main(parsed_cmd->stats_types, parsed_cmd->flags & CMD_FLAG_PER_THREAD_STATS, 0);
      // report_timestamp_history();  // debugging function
   }

bye:
   DBGTRC(main_debug, TRACE_GROUP, "Done.  main_rc=%d", main_rc);

   cur_time = time(NULL);
   cur_time_s = asctime(localtime(&cur_time));
   if (cur_time_s[strlen(cur_time_s)-1] == 0x0a)
      cur_time_s[strlen(cur_time_s)-1] = 0;
   DBGTRC(parsed_cmd && (parsed_cmd->traced_groups || parsed_cmd->traced_functions || parsed_cmd->traced_files),
           TRACE_GROUP,   /* redundant with parsed_cmd->traced_groups */
           "ddcutil execution complete, %s",
           cur_time_s);
   if (parsed_cmd)
      free_parsed_cmd(parsed_cmd);
   release_base_services();
   return main_rc;
}


static void init_rtti() {
   RTTI_ADD_FUNC(main);
   RTTI_ADD_FUNC(execute_cmd_with_optional_display_handle);
   RTTI_ADD_FUNC(find_dref);
#ifdef ENABLE_ENVCMDS
   RTTI_ADD_FUNC(interrogate);
#endif
   init_app_capabilities();
}
