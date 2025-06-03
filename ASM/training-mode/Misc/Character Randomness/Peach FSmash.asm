# To be inserted at 8011c1f4
# Overwrites HSD_Randi call

.include "Common.s"

CharRng_FetchSetting r3, CharRng_Setting_Peach_FSmash, SetResult

cmpwi r3, 0 # 0 = default setting
beq+ RandomFSmash

# reset last fsmash item
li r0, -1
stw r0, 0x2234(r31)

subi r3, r3, 1
b Exit

RandomFSmash:
    li r3, 3
    branchl r12, HSD_Randi

Exit:
