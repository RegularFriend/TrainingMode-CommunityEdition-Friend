#include "../MexTK/mex.h"
#include "events.h"
#include "osds.h"

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

/*
 * A Handoff is defined as a regrab where Climber A grabs the opponent out of a throw from Climber B.

 * If the grab hits, and Climber A's grab hitbox overlaps with Climber B's grab release, it is considered a
 * true handoff and is inescapable. Flash Green.

 * If the Grab hits, and Climber A's grab hitbox is within 10 frames of Climber B's grab release, it is considered
 * a situational handoff, and may be escapable with certain DI and at certain %s. Flash Yellow.
 * TODO: we could have a precompiled table of 'reasonable' situational handoffs to count as full successes and
 * TODO: Flash green. For instance, you don't need the 2f inescapable handoff on donkey kong until well above kill %

 * If Climber A's grab misses and is within N frames of Climber B's throw release, it is considered to be
 * a failed handoff attempt. Flash Red.

 * If Climber A's grab hurtbox ends before Climber B's throw release, it is considered to be a failed handoff.
 * Flash Red.
 */
//Index is player id
typedef struct {
    int ft_throw_begin[6];
    int sub_throw_begin[6];
    int ft_grab_hitbox_begin[6];
    int sub_grab_hitbox_begin[6];
    int ft_handoff_end[6];
    int sub_handoff_end[6];
    int enemy_state_previous[6];
    int enemy_release[6];
} HandoffState;

static bool HandoffOSD_FtHitboxPreviousFrame(const HandoffState* handoff_state, const int id) {
    return event_vars->game_timer - handoff_state->ft_grab_hitbox_begin[id] == 1;
}

static bool HandoffOSD_SubHitboxPreviousFrame(const HandoffState* handoff_state, const int id) {
    return event_vars->game_timer - handoff_state->sub_grab_hitbox_begin[id] == 1;
}

static void RunOSD_Handoff(GOBJ *ft, GOBJ *ft_sub, GOBJ *enm) {
    if (!ft_sub || !ft || !enm) return;
    const int osd_window_after_throw = 120;
    const int handoff_reset_time = 60;
    const int osd_window_after_grab = 15;

    const FighterData *ft_data = ft->userdata;
    const FighterData *sub_data = ft_sub->userdata;
    const FighterData *enemy_data = enm->userdata;

    static HandoffState state = {0};

    const int ft_ply = ft_data->ply;
    const int enemy_ply = enemy_data->ply;

    const bool tick_ft_handoff = state.ft_throw_begin[ft_ply] != 0 &&
                                 event_vars->game_timer - state.ft_throw_begin[ft_ply] < osd_window_after_throw
                                 &&
                                 state.ft_throw_begin[ft_ply] <= event_vars->game_timer;

    const bool tick_sub_handoff = state.sub_throw_begin[ft_ply] != 0 &&
                             event_vars->game_timer - state.sub_throw_begin[ft_ply] < osd_window_after_throw &&
                             state.sub_throw_begin[ft_ply] <= event_vars->game_timer;

    //Primary to sub handoff.
    if (tick_ft_handoff) {
        const bool enemy_released_this_frame = (state.enemy_state_previous[enemy_ply] >= ASID_THROWNF &&
                                                state.enemy_state_previous[enemy_ply] <= ASID_THROWNLWWOMEN)
                                               && (enemy_data->state_id < ASID_THROWNF || enemy_data->state_id >
                                                   ASID_THROWNLWWOMEN);
        if (enemy_released_this_frame) {
            state.enemy_release[enemy_ply] = event_vars->game_timer;
        }

        //check for sub grab hitbox out
        bool sub_grab_hitbox_active = false;
        //ensure its been more than 8 frames from the intial throw to avoid confusion with the synced grab
        if (event_vars->game_timer - state.ft_throw_begin[ft_ply] >= 8 && sub_data->state_id == ASID_CATCH) {
            for (int i = 0; i < 4; i++) {
                if (sub_data->hitbox[i].active) {
                    sub_grab_hitbox_active = true;
                }
            }
        }
        if (sub_grab_hitbox_active && !HandoffOSD_SubHitboxPreviousFrame(&state, ft_ply)) {
            state.sub_grab_hitbox_begin[ft_ply] = event_vars->game_timer;
        }

        //if its been more than 15 frames since the grab hitbox came out, then we consider the handoff a miss and fail.
        //if the sub enters a sucessful grab state, the handoff was a success.
        int grab_to_throw_delta = state.sub_grab_hitbox_begin[ft_ply] - state.enemy_release[enemy_ply];
        if (sub_data->state_id == ASID_CATCHPULL) {
            //if throw release was never caught, that means it was this same frame. same deal for grab hurtbox.
            if (state.enemy_release[enemy_ply] == 0) state.enemy_release[enemy_ply] = event_vars->game_timer;
            if (state.sub_grab_hitbox_begin[ft_ply] == 0)
                state.sub_grab_hitbox_begin[ft_ply] = event_vars->game_timer;
            grab_to_throw_delta = state.sub_grab_hitbox_begin[ft_ply] - state.enemy_release[enemy_ply];

            if (grab_to_throw_delta == -1 || grab_to_throw_delta == 0) {
                Message_Display(OSD_Handoff, ft_ply, MSGCOLOR_GREEN, "Perfect Handoff: %d", grab_to_throw_delta);
            } else {
                Message_Display(OSD_Handoff, ft_ply, MSGCOLOR_YELLOW, "Imperfect Handoff: %d", grab_to_throw_delta);
            }
            state.ft_handoff_end[ft_ply] = event_vars->game_timer;
            state.ft_throw_begin[ft_ply] = 0;
        } else if (state.sub_grab_hitbox_begin[ft_ply] != 0 &&
                   event_vars->game_timer - state.sub_grab_hitbox_begin[ft_ply] > osd_window_after_grab) {
            Message_Display(OSD_Handoff, ft_ply, MSGCOLOR_RED, "Handoff Missed: %d", grab_to_throw_delta);
            state.ft_throw_begin[ft_ply] = 0;
        }

        state.enemy_state_previous[enemy_ply] = enemy_data->state_id;
    } else {
        const bool primary_in_throw = ft_data->state_id >= ASID_THROWF && ft_data->state_id <= ASID_THROWLW;
        const bool primary_osd_on_cooldown = state.ft_handoff_end[ft_ply] != 0 &&
                                             state.ft_handoff_end[ft_ply] <= event_vars->game_timer &&
                                             event_vars->game_timer - state.ft_handoff_end[ft_ply] <
                                             handoff_reset_time;
        if (primary_in_throw && !primary_osd_on_cooldown) {
            state.ft_throw_begin[ft_ply] = event_vars->game_timer;
            state.ft_handoff_end[ft_ply] = 0;
        }

        if (!tick_sub_handoff && !primary_osd_on_cooldown) {
            state.sub_grab_hitbox_begin[ft_ply] = 0;
            state.enemy_state_previous[enemy_ply] = 0;
            state.enemy_release[enemy_ply] = 0;
        }
    }

    //Sub to primary handoff.
    if (tick_sub_handoff) {
        const bool enemy_released_this_frame = (state.enemy_state_previous[enemy_ply] >= ASID_THROWNF &&
                                                state.enemy_state_previous[enemy_ply] <= ASID_THROWNLWWOMEN)
                                               && (enemy_data->state_id < ASID_THROWNF || enemy_data->state_id >
                                                   ASID_THROWNLWWOMEN);
        if (enemy_released_this_frame) {
            state.enemy_release[enemy_ply] = event_vars->game_timer;
        }

        //check for primary grab hitbox out
        bool primary_grab_hitbox_active = false;
        //ensure its been more than 8 frames from the sub's throw to avoid confusion with the synced grab
        if (event_vars->game_timer - state.sub_throw_begin[ft_ply] >= 8 && ft_data->state_id == ASID_CATCH) {
            for (int i = 0; i < 4; i++) {
                if (ft_data->hitbox[i].active) {
                    primary_grab_hitbox_active = true;
                }
            }
        }
        if (primary_grab_hitbox_active && !HandoffOSD_FtHitboxPreviousFrame(&state, ft_ply)) {
            state.ft_grab_hitbox_begin[ft_ply] = event_vars->game_timer;
        }

        int grab_to_throw_delta = state.ft_grab_hitbox_begin[ft_ply] - state.enemy_release[enemy_ply];
        if (ft_data->state_id == ASID_CATCHPULL) {
            if (state.enemy_release[enemy_ply] == 0)
                state.enemy_release[enemy_ply] = event_vars->game_timer;
            if (state.ft_grab_hitbox_begin[ft_ply] == 0)
                state.ft_grab_hitbox_begin[ft_ply] = event_vars->game_timer;
            grab_to_throw_delta = state.ft_grab_hitbox_begin[ft_ply] - state.enemy_release[enemy_ply];

            if (grab_to_throw_delta == -1 || grab_to_throw_delta == 0) {
                Message_Display(OSD_Handoff, ft_ply, MSGCOLOR_GREEN, "Perfect Handoff: %d", grab_to_throw_delta);
            } else {
                Message_Display(OSD_Handoff, ft_ply, MSGCOLOR_YELLOW, "Imperfect Handoff: %d", grab_to_throw_delta);
            }
            state.sub_handoff_end[ft_ply] = event_vars->game_timer;
            state.sub_throw_begin[ft_ply] = 0;
        } else if (state.ft_grab_hitbox_begin[ft_ply] != 0 &&
                   event_vars->game_timer - state.ft_grab_hitbox_begin[ft_ply] > osd_window_after_grab) {
            Message_Display(OSD_Handoff, ft_ply, MSGCOLOR_RED, "Handoff Missed: %d", grab_to_throw_delta);
            state.sub_throw_begin[ft_ply] = 0;
        }

        state.enemy_state_previous[enemy_ply] = enemy_data->state_id;
    } else {
        const bool sub_in_throw = sub_data->state_id >= ASID_THROWF && sub_data->state_id <= ASID_THROWLW;
        const bool sub_osd_on_cooldown = state.sub_handoff_end[ft_ply] != 0 &&
                                         state.sub_handoff_end[ft_ply] <= event_vars->game_timer &&
                                         event_vars->game_timer - state.sub_handoff_end[ft_ply] <
                                         handoff_reset_time;
        if (sub_in_throw && !sub_osd_on_cooldown) {
            state.sub_throw_begin[ft_ply] = event_vars->game_timer;
            state.sub_handoff_end[ft_ply] = 0;
            state.enemy_release[enemy_ply] = 0;
        }

        if (!tick_ft_handoff && !sub_osd_on_cooldown) {
            state.ft_grab_hitbox_begin[ft_ply] = 0;
        }
    }
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
        if (osd_enabled & (1u << OSD_Handoff)) {
            if (ft && ft_sub) {
                for (int enm = 0; enm < 6; ++enm) {
                    if (enm == ply) continue;
                    GOBJ *enm_ft = Fighter_GetSubcharGObj(enm, 0);
                    RunOSD_Handoff(ft, ft_sub, enm_ft);
                }
            }
        }
    }
}
