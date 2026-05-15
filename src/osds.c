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

static void RunOSD_Handoff(GOBJ *ft, GOBJ *ft_sub, GOBJ *enm) {
    if (!ft_sub || !ft || !enm) return;
    const int osd_window_after_throw = 120;
    const int handoff_reset_time = 60;
    const int osd_window_after_grab = 15;

    FighterData *ft_data = ft->userdata;
    FighterData *sub_data = ft_sub->userdata;
    FighterData *enemy_data = enm->userdata;

    // state. arrays use ply as id for lookup.
    static int primary_throw_start_frame[6] = {0};
    static int sub_throw_start_frame[6] = {0};

    static int primary_grab_hitbox_frame_active[6] = {0};
    static int sub_grab_hitbox_frame_active[6] = {0};

    static bool grab_hitbox_last_frame[6] = {0};
    static bool sub_grab_hitbox_last_frame[6] = {0};

    static int primary_handoff_end_frame[6] = {0};
    static int sub_handoff_end_frame[6] = {0};

    static int enemy_state_last_frame[6] = {0};
    static int enemy_released_frame[6] = {0};

    const int active_pid = ft_data->ply;
    const int enemy_pid = enemy_data->ply;

    bool check_primary_handoff = primary_throw_start_frame[active_pid] != 0 &&
                                 event_vars->game_timer - primary_throw_start_frame[active_pid] < osd_window_after_throw
                                 &&
                                 primary_throw_start_frame[active_pid] <= event_vars->game_timer;

    bool check_sub_handoff = sub_throw_start_frame[active_pid] != 0 &&
                             event_vars->game_timer - sub_throw_start_frame[active_pid] < osd_window_after_throw &&
                             sub_throw_start_frame[active_pid] <= event_vars->game_timer;

    //Primary to sub handoff.
    if (check_primary_handoff) {
        const bool enemy_released_this_frame = (enemy_state_last_frame[enemy_pid] >= ASID_THROWNF &&
                                                enemy_state_last_frame[enemy_pid] <= ASID_THROWNLWWOMEN)
                                               && (enemy_data->state_id < ASID_THROWNF || enemy_data->state_id >
                                                   ASID_THROWNLWWOMEN);
        if (enemy_released_this_frame) {
            enemy_released_frame[enemy_pid] = event_vars->game_timer;
        }

        //check for sub grab hitbox out
        bool sub_grab_hitbox_active = false;
        //ensure its been more than 8 frames from the intial throw to avoid confusion with the synced grab
        if (event_vars->game_timer - primary_throw_start_frame[active_pid] >= 8 && sub_data->state_id == ASID_CATCH) {
            for (int i = 0; i < 4; i++) {
                if (sub_data->hitbox[i].active) {
                    sub_grab_hitbox_active = true;
                }
            }
        }
        if (sub_grab_hitbox_active && !sub_grab_hitbox_last_frame[active_pid]) {
            sub_grab_hitbox_frame_active[active_pid] = event_vars->game_timer;
        }

        //if its been more than 15 frames since the grab hitbox came out, then we consider the handoff a miss and fail.
        //if the sub enters a sucessful grab state, the handoff was a success.
        int grab_to_throw_delta = sub_grab_hitbox_frame_active[active_pid] - enemy_released_frame[enemy_pid];
        if (sub_data->state_id >= ASID_CATCHPULL && sub_data->state_id <= ASID_CATCHCUT) {
            //if throw release was never caught, that means it was this same frame. same deal for grab hurtbox.
            if (enemy_released_frame[enemy_pid] == 0) enemy_released_frame[enemy_pid] = event_vars->game_timer;
            if (sub_grab_hitbox_frame_active[active_pid] == 0)
                sub_grab_hitbox_frame_active[active_pid] = event_vars->game_timer;
            grab_to_throw_delta = sub_grab_hitbox_frame_active[active_pid] - enemy_released_frame[enemy_pid];

            if (grab_to_throw_delta == -1 || grab_to_throw_delta == 0) {
                Message_Display(OSD_Handoff, active_pid, MSGCOLOR_GREEN, "Perfect Handoff: %d", grab_to_throw_delta);
            } else {
                Message_Display(OSD_Handoff, active_pid, MSGCOLOR_YELLOW, "Imperfect Handoff: %d", grab_to_throw_delta);
            }
            primary_handoff_end_frame[active_pid] = event_vars->game_timer;
            primary_throw_start_frame[active_pid] = 0;
        } else if (sub_grab_hitbox_frame_active[active_pid] != 0 &&
                   event_vars->game_timer - sub_grab_hitbox_frame_active[active_pid] > osd_window_after_grab) {
            Message_Display(OSD_Handoff, active_pid, MSGCOLOR_RED, "Handoff Missed: %d", grab_to_throw_delta);
            primary_throw_start_frame[active_pid] = 0;
        }

        enemy_state_last_frame[enemy_pid] = enemy_data->state_id;
        sub_grab_hitbox_last_frame[active_pid] = sub_grab_hitbox_active;
    } else {
        const bool primary_in_throw = ft_data->state_id >= ASID_THROWF && ft_data->state_id <= ASID_THROWLW;
        const bool primary_osd_on_cooldown = primary_handoff_end_frame[active_pid] != 0 &&
                                             primary_handoff_end_frame[active_pid] <= event_vars->game_timer &&
                                             event_vars->game_timer - primary_handoff_end_frame[active_pid] <
                                             handoff_reset_time;
        if (primary_in_throw && !primary_osd_on_cooldown) {
            primary_throw_start_frame[active_pid] = event_vars->game_timer;
            primary_handoff_end_frame[active_pid] = 0;
        }

        if (!check_sub_handoff && !primary_osd_on_cooldown) {
            sub_grab_hitbox_frame_active[active_pid] = 0;
            sub_grab_hitbox_last_frame[active_pid] = 0;
            enemy_state_last_frame[enemy_pid] = 0;
            enemy_released_frame[enemy_pid] = 0;
        }
    }

    //Sub to primary handoff.
    if (check_sub_handoff) {
        const bool enemy_released_this_frame = (enemy_state_last_frame[enemy_pid] >= ASID_THROWNF &&
                                                enemy_state_last_frame[enemy_pid] <= ASID_THROWNLWWOMEN)
                                               && (enemy_data->state_id < ASID_THROWNF || enemy_data->state_id >
                                                   ASID_THROWNLWWOMEN);
        if (enemy_released_this_frame) {
            enemy_released_frame[enemy_pid] = event_vars->game_timer;
        }

        //check for primary grab hitbox out
        bool primary_grab_hitbox_active = false;
        //ensure its been more than 8 frames from the sub's throw to avoid confusion with the synced grab
        if (event_vars->game_timer - sub_throw_start_frame[active_pid] >= 8 && ft_data->state_id == ASID_CATCH) {
            for (int i = 0; i < 4; i++) {
                if (ft_data->hitbox[i].active) {
                    primary_grab_hitbox_active = true;
                }
            }
        }
        if (primary_grab_hitbox_active && !grab_hitbox_last_frame[active_pid]) {
            primary_grab_hitbox_frame_active[active_pid] = event_vars->game_timer;
        }

        int grab_to_throw_delta = primary_grab_hitbox_frame_active[active_pid] - enemy_released_frame[enemy_pid];
        if (ft_data->state_id >= ASID_CATCHPULL && ft_data->state_id <= ASID_CATCHCUT) {
            if (enemy_released_frame[enemy_pid] == 0)
                enemy_released_frame[enemy_pid] = event_vars->game_timer;
            if (primary_grab_hitbox_frame_active[active_pid] == 0)
                primary_grab_hitbox_frame_active[active_pid] = event_vars->game_timer;
            grab_to_throw_delta = primary_grab_hitbox_frame_active[active_pid] - enemy_released_frame[enemy_pid];

            if (grab_to_throw_delta == -1 || grab_to_throw_delta == 0) {
                Message_Display(OSD_Handoff, active_pid, MSGCOLOR_GREEN, "Perfect Handoff: %d", grab_to_throw_delta);
            } else {
                Message_Display(OSD_Handoff, active_pid, MSGCOLOR_YELLOW, "Imperfect Handoff: %d", grab_to_throw_delta);
            }
            sub_handoff_end_frame[active_pid] = event_vars->game_timer;
            sub_throw_start_frame[active_pid] = 0;
        } else if (primary_grab_hitbox_frame_active[active_pid] != 0 &&
                   event_vars->game_timer - primary_grab_hitbox_frame_active[active_pid] > osd_window_after_grab) {
            Message_Display(OSD_Handoff, active_pid, MSGCOLOR_RED, "Handoff Missed: %d", grab_to_throw_delta);
            sub_throw_start_frame[active_pid] = 0;
        }

        enemy_state_last_frame[enemy_pid] = enemy_data->state_id;
        grab_hitbox_last_frame[active_pid] = primary_grab_hitbox_active;
    } else {
        const bool sub_in_throw = sub_data->state_id >= ASID_THROWF && sub_data->state_id <= ASID_THROWLW;
        const bool sub_osd_on_cooldown = sub_handoff_end_frame[active_pid] != 0 &&
                                         sub_handoff_end_frame[active_pid] <= event_vars->game_timer &&
                                         event_vars->game_timer - sub_handoff_end_frame[active_pid] <
                                         handoff_reset_time;
        if (sub_in_throw && !sub_osd_on_cooldown) {
            sub_throw_start_frame[active_pid] = event_vars->game_timer;
            sub_handoff_end_frame[active_pid] = 0;
            enemy_released_frame[enemy_pid] = 0;
        }

        if (!check_primary_handoff && !sub_osd_on_cooldown) {
            primary_grab_hitbox_frame_active[active_pid] = 0;
            grab_hitbox_last_frame[active_pid] = 0;
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
