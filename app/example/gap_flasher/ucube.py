src     = Split('''
        helloworld.c
''')

component = aos_component('helloworld', src)
component.add_comp_deps('kernel/yloop', 'kernel/cli')
component.add_global_macros('AOS_NO_WIFI')
