/* Native PRX module information and lifecycle exports. */
.section .module_info,"a"
.balign 4
.global presence_module_info
presence_module_info:
    .short 0
    .byte 0,4
    .asciz "ps3_presence"
    .space 15
    .long __presence_toc
    .long presence_exports
    .long presence_exports_end
    .long presence_imports
    .long presence_imports_end
.section .module_exports,"a"
.balign 4
presence_exports:
    .byte 28,0
    .short 0
    .short 0x8000
    .short 2
    .short 1,0
    .byte 0,0,0,0
    .long 0
    .long presence_nids
    .long presence_addrs
presence_exports_end:
presence_nids:
    .long 0xbc9a0086,0xab779874,0xd7f43016
presence_addrs:
    .long presence_start_opd,presence_stop_opd,presence_module_info
.section .module_imports,"a"
.balign 4
presence_imports:
    .byte 44,0
    .short 1,1,2,0,0
    .long 0,presence_sysprx_name,presence_thread_nid,presence_thread_create_import
    .long 0,0,0,0
presence_imports_end:
presence_sysprx_name:
    .asciz "sysPrxForUser"
.balign 4
presence_thread_nid:
    .long 0x24a1ea07,0xaff080a4
.section .data,"aw"
.balign 4
.global presence_thread_create_import
presence_thread_create_import:
    .long 0
.global presence_thread_exit_import
presence_thread_exit_import:
    .long 0
presence_start_opd:
    .long __presence_start_code,__presence_toc
presence_stop_opd:
    .long __presence_stop_code,__presence_toc

.section .opd,"aw"
.balign 8
.global vsh_call
vsh_call:
    .quad .Lvsh_call,.TOC.@tocbase,0
.section .text.vsh_call,"ax"
.balign 4
.Lvsh_call:
    mflr 0
    std 0,16(1)
    std 2,40(1)
    stdu 1,-128(1)
    mr 12,3
    mr 3,4
    mr 4,5
    mr 5,6
    mr 6,7
    mr 7,8
    mr 8,9
    mr 9,10
    lwz 0,0(12)
    lwz 2,4(12)
    mtctr 0
    bctrl
    addi 1,1,128
    ld 2,40(1)
    ld 0,16(1)
    mtlr 0
    blr
