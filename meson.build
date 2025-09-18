project('mzip', 'c',
  version : '0.1.0',
  default_options : ['c_std=c99'])

executable('mzip', 'main.c', 'mzip.c',
  dependencies : [dependency('zlib')],
  install : true)
