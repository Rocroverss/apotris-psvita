if not opus_conf.has('OPUS_ARM_MAY_HAVE_EDSP')
  opus_conf.set('OPUS_ARM_MAY_HAVE_EDSP', 0)
endif
if not opus_conf.has('OPUS_ARM_MAY_HAVE_MEDIA')
  opus_conf.set('OPUS_ARM_MAY_HAVE_MEDIA', 0)
endif
if not opus_conf.has('OPUS_ARM_MAY_HAVE_NEON')
  opus_conf.set('OPUS_ARM_MAY_HAVE_NEON', 0)
endif
arm2gnu = [find_program('arm2gnu.pl')] + arm2gnu_args
celt_sources_arm_asm = configure_file(input: 'celt_pitch_xcorr_arm.s',
  output: '@BASENAME@-gnu.S',
  command: arm2gnu + ['@INPUT@'],
  capture: true)
celt_arm_armopts_s_in = configure_file(input: 'armopts.s.in',
  output: 'armopts.s',
  configuration: opus_conf)
celt_arm_armopts_s = configure_file(input: [celt_arm_armopts_s_in],
  output: '@BASENAME@-gnu.S',
  command: arm2gnu + ['@INPUT@'],
  capture: true)
