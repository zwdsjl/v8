# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import json
import optparse
import os
import sys


# Add testrunner to the path.
sys.path.insert(
  0,
  os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))


from local import utils


BASE_DIR = (
    os.path.dirname(
      os.path.dirname(
        os.path.dirname(
          os.path.abspath(__file__)))))

DEFAULT_OUT_GN = 'out.gn'

ARCH_GUESS = utils.DefaultArch()

# Map of test name synonyms to lists of test suites. Should be ordered by
# expected runtimes (suites with slow test cases first). These groups are
# invoked in separate steps on the bots.
TEST_MAP = {
  # This needs to stay in sync with test/bot_default.isolate.
  "bot_default": [
    "debugger",
    "mjsunit",
    "cctest",
    "wasm-spec-tests",
    "inspector",
    "webkit",
    "mkgrokdump",
    "fuzzer",
    "message",
    "preparser",
    "intl",
    "unittests",
  ],
  # This needs to stay in sync with test/default.isolate.
  "default": [
    "debugger",
    "mjsunit",
    "cctest",
    "wasm-spec-tests",
    "inspector",
    "mkgrokdump",
    "fuzzer",
    "message",
    "preparser",
    "intl",
    "unittests",
  ],
  # This needs to stay in sync with test/optimize_for_size.isolate.
  "optimize_for_size": [
    "debugger",
    "mjsunit",
    "cctest",
    "inspector",
    "webkit",
    "intl",
  ],
  "unittests": [
    "unittests",
  ],
}


class ModeConfig(object):
  def __init__(self, flags, timeout_scalefactor, status_mode, execution_mode):
    self.flags = flags
    self.timeout_scalefactor = timeout_scalefactor
    self.status_mode = status_mode
    self.execution_mode = execution_mode


DEBUG_FLAGS = ["--nohard-abort", "--enable-slow-asserts", "--verify-heap"]
RELEASE_FLAGS = ["--nohard-abort"]
MODES = {
  "debug": ModeConfig(
    flags=DEBUG_FLAGS,
    timeout_scalefactor=4,
    status_mode="debug",
    execution_mode="debug",
  ),
  "optdebug": ModeConfig(
    flags=DEBUG_FLAGS,
    timeout_scalefactor=4,
    status_mode="debug",
    execution_mode="debug",
  ),
  "release": ModeConfig(
    flags=RELEASE_FLAGS,
    timeout_scalefactor=1,
    status_mode="release",
    execution_mode="release",
  ),
  # Normal trybot release configuration. There, dchecks are always on which
  # implies debug is set. Hence, the status file needs to assume debug-like
  # behavior/timeouts.
  "tryrelease": ModeConfig(
    flags=RELEASE_FLAGS,
    timeout_scalefactor=1,
    status_mode="debug",
    execution_mode="release",
  ),
  # This mode requires v8 to be compiled with dchecks and slow dchecks.
  "slowrelease": ModeConfig(
    flags=RELEASE_FLAGS + ["--enable-slow-asserts"],
    timeout_scalefactor=2,
    status_mode="debug",
    execution_mode="release",
  ),
}


class TestRunnerError(Exception):
  pass


class BuildConfig(object):
  def __init__(self, build_config):
    # In V8 land, GN's x86 is called ia32.
    if build_config['v8_target_cpu'] == 'x86':
      self.arch = 'ia32'
    else:
      self.arch = build_config['v8_target_cpu']

    self.is_debug = build_config['is_debug']
    self.asan = build_config['is_asan']
    self.cfi_vptr = build_config['is_cfi']
    self.dcheck_always_on = build_config['dcheck_always_on']
    self.gcov_coverage = build_config['is_gcov_coverage']
    self.msan = build_config['is_msan']
    self.no_i18n = not build_config['v8_enable_i18n_support']
    self.no_snap = not build_config['v8_use_snapshot']
    self.predictable = build_config['v8_enable_verify_predictable']
    self.tsan = build_config['is_tsan']
    self.ubsan_vptr = build_config['is_ubsan_vptr']


class BaseTestRunner(object):
  def __init__(self):
    self.outdir = None
    self.shell_dir = None
    self.build_config = None
    self.mode_name = None
    self.mode_options = None

  def execute(self):
    try:
      parser = self._create_parser()
      options, args = self._parse_args(parser)

      self._load_build_config(options)

      try:
        self._process_default_options(options)
        self._process_options(options)
      except TestRunnerError:
        parser.print_help()
        raise

      return self._do_execute(options, args)
    except TestRunnerError:
      return 1

  def _create_parser(self):
    parser = optparse.OptionParser()
    parser.usage = '%prog [options] [tests]'
    parser.description = """TESTS: %s""" % (TEST_MAP["default"])
    self._add_parser_default_options(parser)
    self._add_parser_options(parser)
    return parser

  def _add_parser_default_options(self, parser):
    parser.add_option("--gn", help="Scan out.gn for the last built"
                      " configuration",
                      default=False, action="store_true")
    parser.add_option("--outdir", help="Base directory with compile output",
                      default="out")
    parser.add_option("--buildbot",
                      help="Adapt to path structure used on buildbots",
                      default=False, action="store_true")
    parser.add_option("--arch",
                      help="The architecture to run tests for: %s")
    parser.add_option("-m", "--mode",
                      help="The test mode in which to run (uppercase for ninja"
                      " and buildbot builds): %s" % MODES.keys())
    parser.add_option("--shell", help="DEPRECATED! use --shell-dir",
                      default="")
    parser.add_option("--shell-dir", help="Directory containing executables",
                      default="")

  def _add_parser_options(self, parser):
    pass

  def _parse_args(self, parser):
    options, args = parser.parse_args()

    if any(map(lambda v: v and ',' in v,
                [options.arch, options.mode])):
      print 'Multiple arch/mode are deprecated'
      raise TestRunnerError()

    return options, args

  def _load_build_config(self, options):
    for outdir in self._possible_outdirs(options):
      try:
        self.build_config = self._do_load_build_config(
          outdir, options.mode, options.buildbot)
      except TestRunnerError:
        pass

    if not self.build_config:
      print 'Failed to load build config'
      raise TestRunnerError

    print 'Build found: %s' % self.outdir

  # Returns possible build paths in order: gn, outdir, outdir/arch.mode
  def _possible_outdirs(self, options):
    if options.gn:
      yield self._get_gn_outdir()
      return

    yield options.outdir
    if options.arch and options.mode:
      yield os.path.join(options.outdir,
                         '%s.%s' % (options.arch, options.mode))
    return

  def _get_gn_outdir(self):
    gn_out_dir = os.path.join(BASE_DIR, DEFAULT_OUT_GN)
    latest_timestamp = -1
    latest_config = None
    for gn_config in os.listdir(gn_out_dir):
      gn_config_dir = os.path.join(gn_out_dir, gn_config)
      if not os.path.isdir(gn_config_dir):
        continue
      if os.path.getmtime(gn_config_dir) > latest_timestamp:
        latest_timestamp = os.path.getmtime(gn_config_dir)
        latest_config = gn_config
    if latest_config:
      print(">>> Latest GN build found: %s" % latest_config)
      return os.path.join(DEFAULT_OUT_GN, latest_config)

  def _do_load_build_config(self, outdir, mode, is_buildbot):
    if is_buildbot:
      build_config_path = os.path.join(
        BASE_DIR, outdir, mode, "v8_build_config.json")
    else:
      build_config_path = os.path.join(
        BASE_DIR, outdir, "v8_build_config.json")
    if not os.path.exists(build_config_path):
      raise TestRunnerError()

    with open(build_config_path) as f:
      try:
        build_config_json = json.load(f)
      except Exception:
        print("%s exists but contains invalid json. Is your build up-to-date?"
              % build_config_path)
        raise TestRunnerError()

    # In auto-detect mode the outdir is always where we found the build
    # config.
    # This ensures that we'll also take the build products from there.
    self.outdir = os.path.dirname(build_config_path)

    return BuildConfig(build_config_json)

  def _process_default_options(self, options):
    # We don't use the mode for more path-magic.
    # Therefore transform the buildbot mode here to fix build_config value.
    if options.buildbot and options.mode:
      options.mode = self._buildbot_to_v8_mode(options.mode)

    build_config_mode = 'debug' if self.build_config.is_debug else 'release'
    if options.mode:
      if options.mode not in MODES:
        print '%s mode is invalid' % options.mode
        raise TestRunnerError()
      if MODES[options.mode].execution_mode != build_config_mode:
        print ('execution mode (%s) for %s is inconsistent with build config '
               '(%s)' % (
            MODES[options.mode].execution_mode,
            options.mode,
            build_config_mode))
        raise TestRunnerError()

      self.mode_name = options.mode
    else:
      self.mode_name = build_config_mode

    self.mode_options = MODES[self.mode_name]

    if options.arch and options.arch != self.build_config.arch:
      print('--arch value (%s) inconsistent with build config (%s).' % (
        options.arch, self.build_config.arch))
      raise TestRunnerError()

    self._set_shell_dir(options)

  def _buildbot_to_v8_mode(self, config):
    """Convert buildbot build configs to configs understood by the v8 runner.

    V8 configs are always lower case and without the additional _x64 suffix
    for 64 bit builds on windows with ninja.
    """
    mode = config[:-4] if config.endswith('_x64') else config
    return mode.lower()

  def _set_shell_dir(self, options):
    self.shell_dir = options.shell_dir
    if not self.shell_dir:
      # TODO(majeski): drop this option
      if options.shell:
        print "Warning: --shell is deprecated, use --shell-dir instead."
        self.shell_dir = os.path.dirname(options.shell)
      else:
        # If an output dir with a build was passed, test directly in that
        # directory.
        self.shell_dir = os.path.join(BASE_DIR, self.outdir)
      if not os.path.exists(self.shell_dir):
          raise Exception('Could not find shell_dir: "%s"' % self.shell_dir)

  def _process_options(self, options):
    pass

  # TODO(majeski): remove options & args parameters
  def _do_execute(self, options, args):
    raise NotImplementedError()
