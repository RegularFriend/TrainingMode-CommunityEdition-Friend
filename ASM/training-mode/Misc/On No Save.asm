    # To be inserted at 801af6f4
    .include "../../Globals.s"

    .set MEMCARD_HASSAVE, 0x0
    .set MEMCARD_NOSAVE, 0x4
    .set MEMCARD_NONE, 0xF
    .set MEMCARD_NONE2, 0xD

    # Check if no memcard inserted
    cmpwi r29, MEMCARD_HASSAVE
    beq Original
    cmpwi r29, MEMCARD_NOSAVE
    beq NoSave
    cmpwi r29, MEMCARD_NONE
    beq NoMemcard
    cmpwi r29, MEMCARD_NONE2
    beq NoMemcard

NoSave:
    bl InitSave
    b Original

NoMemcard:
    bl InitSave

    # Exit memcard think and disable saving
    branch r12, 0x801b01ac

InitSave:
    backup

    # Get trophy data
    lwz r3, MemcardData(r13)
    addi r3, r3, 7376
    # Set trophy count
    li r4, 293
    sth r4, 0x0(r3)
    # Set individual trophies as unlocked
    addi r3, r3, 4
    li r4, 99
    li r5, 0x24f
    branchl r12, memset

InitSettings:
    # Set Max OSD on No Memcard
    li r3, 1
    lwz r4, MemcardData(r13)
    stb r3, OSDMaxWindows(r4)
    # Set Initial Page Number
    li r3, 0x1
    stb r3, CurrentEventPage(r4)
    # Enable Recommended OSDs
    li r3, 0
    stb r3, OSDRecommended(r4)
    # Turn off OSDs by default
    li r3, 0
    stw r3, OSDBitfield(r4)

    restore
    blr

Original:
    cmpwi r29, 0
