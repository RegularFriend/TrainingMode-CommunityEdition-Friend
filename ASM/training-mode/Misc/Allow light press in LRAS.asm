    # To be inserted at 8016d1f8
    # In SceneThink_VSMove, just before the branch to skip the lras check

    .include "../Globals.s"
    .include "../../m-ex/Header.s"
    
    load r3, 0x804c20bc # stc_css_pad
    lwz r4, 0x0(r3) # held
    
CheckStart:
    andi. r0, r4, PAD_BUTTON_START
    beq False
    
CheckA:
    andi. r0, r4, PAD_BUTTON_A
    beq False
    
CheckL:
    lbz r5, 0x1C(r3) # triggerLeft
    cmpwi r5, 20
    bge CheckLEnd
    andi. r0, r4, PAD_TRIGGER_L
    bne CheckLEnd
    b False
CheckLEnd:
    
CheckR:
    lbz r5, 0x1D(r3) # triggerRight
    cmpwi r5, 20
    bge CheckREnd
    andi. r0, r4, PAD_TRIGGER_R
    bne CheckREnd
    b False
CheckREnd:

    li r3, 1
    b Exit
False:
    li r3, 0
Exit:
    cmpwi r3, 1
