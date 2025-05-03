    # To be inserted at 801b02ec
    .include "../../Globals.s"
    .include "../../m-ex/Header.s"

    # Set CPU Info
    li r3, 0x21
    stb r3, EventCPUBackup_CharID(rtoc)                 # Set CPU Character ID

Original:
    lwz r0, 0x001C(sp)
