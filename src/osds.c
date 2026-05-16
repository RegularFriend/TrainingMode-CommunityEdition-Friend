#include "../MexTK/mex.h"
#include "events.h"
#include "osds.h"

static GXColor stc_msg_colors[] = {
    {255, 255, 255, 255},
    {141, 255, 110, 255},
    {255, 162, 186, 255},
    {255, 240, 0, 255},
};

static void UpdateIASATracking(GOBJ *ft) {
    FighterData *ft_data = ft->userdata;
    if (ft_data->flags.past_iasa)
        ft_data->TM.iasa_frames++;
    else
        ft_data->TM.iasa_frames = 0;
}

static bool CheckIASA(FighterData *data) {
    // The spotdodge state is weird - its subaction list contains an IASA event which sets the IASA bit,
    // but the initial state entry function does not clear the flag.
    // This causes it to not function - the player can't cancel it,
    // but it causes us to think they can if it is performed after a state that clears the iasa bit (e.g. landing with an aerial).
    if (data->state_id == ASID_ESCAPE)
        return false;

    // For some reason, the iasa flag isn't reset when the state changes, only when a new action that has iasa starts.
    // So we need to do some bs tracking to figure out if the character is in iasa.
    return data->TM.iasa_frames && data->TM.iasa_frames <= data->TM.state_frame;
}

static void RunOSD_FrameAdvantage(GOBJ *ft, GOBJ *ft_sub) {
    /*
    Detect when your shield is hit. This is ez, look for GuardSetOff.
        If the attacker is actionable and the defender is not,
            the advantage is the # frames until shieldstun is gone.
        If the attacker is actionable and the defender is also actionable,
            the advantages is -frames since shieldstun.

    We use the 0 state to wait for GuardSetOff to start.
    We use the -1 state to wait for GuardSetOff to end.
    */

    FighterData *ft_data = ft->userdata;
    FighterData *ft_def_data = ft_data->TM.fighter_hurt_shield;
    if (ft_def_data == 0) return;
    GOBJ *ft_def = ft_def_data->fighter;

    int ply = ft_data->ply;
    static int atk_hit_state[6] = {0};

    // Wait for GuardSetOff to end before running frame advantage code again
    if (atk_hit_state[ply] == -1) {
        if (ft_def_data->state_id != ASID_GUARDSETOFF)
            atk_hit_state[ply] = 0;
        return;
    }

    // Detect when the defender's shield is hit and save the attacker's state
    if (atk_hit_state[ply] == 0) {
        if (ft_def_data->state_id == ASID_GUARDSETOFF)
            atk_hit_state[ply] = ft_data->state_id;
        return;
    }

    int atk_state = ft_data->state_id;

    // "actionable" if either state changed or in iasa, and not in aerial or ac landing lag.
    bool atk_actionable = (atk_hit_state[ply] != atk_state || CheckIASA(ft_data))
                          && (atk_state != ASID_LANDING || ft_data->state.frame >= ft_data->attr.normal_landing_lag)
                          && (atk_state < ASID_LANDINGAIRN || ASID_LANDINGAIRLW < atk_state);

    if (atk_actionable) {
        int advantage;

        if (ft_def_data->state_id == ASID_GUARDSETOFF) {
            // + on shield: advantage is frames until shieldstun is gone.
            float anim_speed = ft_def_data->state.rate;
            float stun_total = JOBJ_GetJointAnimFrameTotal(ft_def->hsd_object);
            float stun_curr = ft_def_data->state.frame;
            advantage = (int) ((stun_total - stun_curr) / anim_speed) + 1;
        } else {
            // = or - on shield: advantage is -frames since shieldstun.
            advantage = -(int) ft_def_data->TM.state_frame;
            for (u32 i = 0; i < countof(ft_def_data->TM.state_prev); ++i) {
                if (ft_def_data->TM.state_prev[i] == ASID_GUARDSETOFF)
                    break;
                advantage -= (int) ft_def_data->TM.state_prev_frames[i];
            }
            advantage += 1; // off-by-one
        }

        Message_Display(
            OSD_FrameAdvantage, ft_data->ply,
            advantage > -7 ? MSGCOLOR_GREEN : MSGCOLOR_WHITE,
            "Frame Advantage\n%d Frames", advantage
        );

        // Wait until GuardSetOff finishes
        atk_hit_state[ply] = -1;
    }
}

typedef struct {
    int ft_handoff_begin[6];
    int sub_handoff_begin[6];
    int ft_grab_hitbox_begin[6];
    int sub_grab_hitbox_begin[6];
    int ft_state_previous[6];
    int sub_state_previous[6];
    int enemy_state_previous[6];
    int enemy_release[6];
} HandoffState;


static bool OSDHandoff_FtGrabHitboxActiveLastFrame(const HandoffState *handoff_state, const int id) {
    return event_vars->game_timer - handoff_state->ft_grab_hitbox_begin[id] == 1;
}

static bool OSDHandoff_SubGrabHitboxActiveLastFrame(const HandoffState *handoff_state, const int id) {
    return event_vars->game_timer - handoff_state->sub_grab_hitbox_begin[id] == 1;
}

static bool IsThrowState(const int state_id) {
    return state_id >= ASID_THROWF && state_id <= ASID_THROWLW;
}

static bool IsThrownState(const int state_id) {
    return state_id >= ASID_THROWNF && state_id <= ASID_THROWNLWWOMEN;
}

static bool IsGrabHitboxActive(const FighterData *ft_data) {
    if (ft_data->state_id == ASID_CATCH) {
        for (int i = 0; i < 4; i++) {
            if (ft_data->hitbox[i].active) {
                return true;
            }
        }
    }
    return false;
}

static void RunOSD_Handoff(GOBJ *ft, GOBJ *ft_sub, GOBJ *enm) {
    //icies check
    if (!ft_sub || !ft || !enm) return;
    static HandoffState state = {0};

    //tweakables
    const int timeout = 60;
    const int grab_miss_timeout = 15;

    const FighterData *ft_data = ft->userdata;
    const FighterData *sub_data = ft_sub->userdata;
    const FighterData *enemy_data = enm->userdata;
    const int ft_ply = ft_data->ply;
    const int enemy_ply = enemy_data->ply;

    //tick if they've thrown recently and not in the future (in the case of the reset)
    const bool tick_ft_handoff = state.ft_handoff_begin[ft_ply] != 0 &&
                                 event_vars->game_timer - state.ft_handoff_begin[ft_ply] < timeout &&
                                 state.ft_handoff_begin[ft_ply] <= event_vars->game_timer;
    const bool tick_sub_handoff = state.sub_handoff_begin[ft_ply] != 0 &&
                                  event_vars->game_timer - state.sub_handoff_begin[ft_ply] < timeout &&
                                  state.sub_handoff_begin[ft_ply] <= event_vars->game_timer;

    //TICK FT -> SUB HANDOFF
    if (tick_ft_handoff) {
        const bool enemy_released = IsThrownState(state.enemy_state_previous[enemy_ply]) && !IsThrownState(
                                        enemy_data->state_id);
        if (enemy_released) {
            state.enemy_release[enemy_ply] = event_vars->game_timer;
        }
        //wait a few frames after the handoff begins to start looking for the other to throw. to avoid synced throws
        //falss reporting a miss on a 'very early nana grab'
        if (event_vars->game_timer - state.ft_handoff_begin[ft_ply] > 8 && IsGrabHitboxActive(sub_data) && !OSDHandoff_SubGrabHitboxActiveLastFrame(&state, ft_ply)) {
            state.sub_grab_hitbox_begin[ft_ply] = event_vars->game_timer;
        }

        //if you grabbed them, handoff complete.
        if (sub_data->state_id == ASID_CATCHPULL) {
            //if throw release was never caught, that means it was this same frame. same deal for grab hurtbox.
            if (state.enemy_release[enemy_ply] == 0) state.enemy_release[enemy_ply] = event_vars->game_timer;
            if (state.sub_grab_hitbox_begin[ft_ply] == 0) state.sub_grab_hitbox_begin[ft_ply] = event_vars->game_timer;

            int grab_to_throw_delta = state.sub_grab_hitbox_begin[ft_ply] - state.enemy_release[enemy_ply];
            bool grab_was_early = grab_to_throw_delta < 1;
            int color_timing = grab_to_throw_delta == -1 || grab_to_throw_delta == 0 ? MSGCOLOR_GREEN : MSGCOLOR_WHITE;
            GOBJ *msg_gobj = Message_Display(OSD_FighterSpecificTech, ft_ply, MSGCOLOR_WHITE, "Handoff Success\n %dF %s", abs(grab_to_throw_delta), grab_was_early ? "early" : "late");
            MsgData *msg = msg_gobj->userdata;
            Text_SetColor(msg->text, 0, &stc_msg_colors[MSGCOLOR_GREEN]);
            Text_SetColor(msg->text, 1, &stc_msg_colors[color_timing]);
            state.ft_handoff_begin[ft_ply] = 0;
        }
        //if you threw out a grab, and it didn't grab by the timeout, you tried to handoff and failed.
        else if (state.sub_grab_hitbox_begin[ft_ply] != 0 && event_vars->game_timer - state.sub_grab_hitbox_begin[ft_ply] > grab_miss_timeout) {
            int grab_to_throw_delta = state.sub_grab_hitbox_begin[ft_ply] - state.enemy_release[enemy_ply];
            bool grab_was_early = grab_to_throw_delta < 1;
            Message_Display(OSD_FighterSpecificTech, ft_ply, MSGCOLOR_RED, "Handoff Failure\n %dF %s", abs(grab_to_throw_delta), grab_was_early ? "early" : "late");
            state.ft_handoff_begin[ft_ply] = 0;
        }

    } else {
        //Don't start a new handoff if you were throwing last frame.
        if (!IsThrowState(state.ft_state_previous[ft_ply])) {
            //if you're throwing this frame, a handoff begins
            if (IsThrowState(ft_data->state_id)) {
                state.ft_handoff_begin[ft_ply] = event_vars->game_timer;
            }
            //reset state
            if (!tick_sub_handoff) {
                state.sub_grab_hitbox_begin[ft_ply] = 0;
                state.enemy_state_previous[enemy_ply] = 0;
                state.enemy_release[enemy_ply] = 0;
            }
        }
    }

    //Sub to primary handoff.
    if (tick_sub_handoff) {
        const bool enemy_released = IsThrownState(state.enemy_state_previous[enemy_ply]) && !IsThrownState(
                                        enemy_data->state_id);
        if (enemy_released) {
            state.enemy_release[enemy_ply] = event_vars->game_timer;
        }
        //wait a few frames after the handoff begins to start looking for the other to throw. to avoid synced throws
        //falss reporting a miss on a 'very early nana grab'
        if (event_vars->game_timer - state.sub_handoff_begin[ft_ply] > 8 && IsGrabHitboxActive(ft_data) && !OSDHandoff_FtGrabHitboxActiveLastFrame(&state, ft_ply)) {
            state.ft_grab_hitbox_begin[ft_ply] = event_vars->game_timer;
        }

        if (ft_data->state_id == ASID_CATCHPULL) {
            //if throw release was never caught, that means it was this same frame. same deal for grab hurtbox.
            if (state.enemy_release[enemy_ply] == 0) state.enemy_release[enemy_ply] = event_vars->game_timer;
            if (state.ft_grab_hitbox_begin[ft_ply] == 0) state.ft_grab_hitbox_begin[ft_ply] = event_vars->game_timer;

            int grab_to_throw_delta = state.ft_grab_hitbox_begin[ft_ply] - state.enemy_release[enemy_ply];
            bool grab_was_early = grab_to_throw_delta < 1;
            int color_timing = grab_to_throw_delta == -1 || grab_to_throw_delta == 0 ? MSGCOLOR_GREEN : MSGCOLOR_WHITE;
            GOBJ *msg_gobj = Message_Display(OSD_FighterSpecificTech, ft_ply, MSGCOLOR_WHITE, "Handoff Success\n %dF %s", abs(grab_to_throw_delta), grab_was_early ? "early" : "late");
            MsgData *msg = msg_gobj->userdata;
            Text_SetColor(msg->text, 0, &stc_msg_colors[MSGCOLOR_GREEN]);
            Text_SetColor(msg->text, 1, &stc_msg_colors[color_timing]);
            state.sub_handoff_begin[ft_ply] = 0;
        } else if (state.ft_grab_hitbox_begin[ft_ply] != 0 &&
                   event_vars->game_timer - state.ft_grab_hitbox_begin[ft_ply] > grab_miss_timeout) {
            int grab_to_throw_delta = state.ft_grab_hitbox_begin[ft_ply] - state.enemy_release[enemy_ply];
            bool grab_was_early = grab_to_throw_delta < 1;
             Message_Display(OSD_FighterSpecificTech, ft_ply, MSGCOLOR_RED, "Handoff Failure\n %dF %s", abs(grab_to_throw_delta), grab_was_early ? "early" : "late");
            state.sub_handoff_begin[ft_ply] = 0;
        }

    } else {
        //Don't start a new handoff if you were throwing last frame.
        if (!IsThrowState(state.sub_state_previous[ft_ply])) {
            //if you're throwing this frame, a handoff begins
            if (IsThrowState(sub_data->state_id)) {
                state.sub_handoff_begin[ft_ply] = event_vars->game_timer;
            }
            //reset state
            if (!tick_ft_handoff) {
                state.ft_grab_hitbox_begin[ft_ply] = 0;
                state.enemy_release[enemy_ply] = 0;
            }
        }
    }
    state.enemy_state_previous[enemy_ply] = enemy_data->state_id;
    state.ft_state_previous[ft_ply] = ft_data->state_id;
    state.sub_state_previous[ft_ply] = sub_data->state_id;
}


void OSD_Think(GOBJ *event) {
    u32 osd_enabled = stc_memcard->TM_OSDEnabled;

    for (int ply = 0; ply < 6; ++ply) {
        GOBJ *ft = Fighter_GetSubcharGObj(ply, 0);
        if (!ft) continue;

        // subchar is usually null except for nana and the inactive sheik/zelda tform
        GOBJ *ft_sub = Fighter_GetSubcharGObj(ply, 1);

        if (ft) UpdateIASATracking(ft);
        if (ft_sub) UpdateIASATracking(ft_sub);


        if (osd_enabled & (1u << OSD_FrameAdvantage)) RunOSD_FrameAdvantage(ft, ft_sub);
        if (osd_enabled & (1u << OSD_FighterSpecificTech)) {
            //Handoff OSD. Checks for ice climbers within the function.
            for (int enm = 0; enm < 6; ++enm) {
                if (enm == ply) continue;
                GOBJ *enm_ft = Fighter_GetSubcharGObj(enm, 0);
                RunOSD_Handoff(ft, ft_sub, enm_ft);
            }
        }
    }
}
