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
            advantage = (int)((stun_total - stun_curr) / anim_speed) + 1;
        } else {
            // = or - on shield: advantage is -frames since shieldstun.
            advantage = -(int)ft_def_data->TM.state_frame;
            for (u32 i = 0; i < countof(ft_def_data->TM.state_prev); ++i) {
                if (ft_def_data->TM.state_prev[i] == ASID_GUARDSETOFF)
                    break;
                advantage -= (int)ft_def_data->TM.state_prev_frames[i];
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
static void RunOSD_Handoff(GOBJ *grabber, GOBJ *thrower, GOBJ *enemy, GOBJ *enemy_sub) {
    if (!grabber || !thrower || !enemy) return;

    FighterData *grabber_data = grabber->userdata;
    FighterData *thrower_data = thrower->userdata;

    static int throw_release_frames[6] = {0};
    static int grabber_hitbox_frames[6] = {0};

    static int enemy_prev_states[6] = {0};
    static int enemy_sub_prev_states[6] = {0};

    // Huge assumptions being made here about the specific ordering of state enums.
    if (thrower_data->state_id >= ASID_THROWF && thrower_data->state_id <= ASID_THROWLW) {
        const FighterData *enemy_data = enemy->userdata;

        bool released_this_frame = enemy_data->state_id >= ASID_THROWNF
                     && enemy_data->state_id <= ASID_THROWNLWWOMEN
                     && enemy_prev_states[enemy_data->ply] < ASID_THROWNF;

        if (enemy_sub) {
            FighterData *enemy_sub_data = enemy_sub->userdata;

            released_this_frame |= enemy_sub_data->state_id >= ASID_THROWNF
                     && enemy_sub_data->state_id <= ASID_THROWNLWWOMEN
                     && enemy_sub_prev_states[enemy_sub_data->ply] < ASID_THROWNF;

            enemy_sub_prev_states[enemy_sub_data->ply] = enemy_sub_data->state_id;
        }

        enemy_prev_states[enemy_data->ply] = enemy_data->state_id;
        if (released_this_frame) throw_release_frames[thrower_data->ply] = event_vars->game_timer;
    }

    const bool grabber_hitbox_active = grabber_data->throw_hitbox[0].active || grabber_data->throw_hitbox[1].active;
    if (grabber_hitbox_active) {
        //icies grab has two active frames. only record the first one
        if (grabber_hitbox_frames[grabber_data->ply] == event_vars->game_timer -1) return;
        grabber_hitbox_frames[grabber_data->ply] = event_vars->game_timer;
    }

    const int throw_grab_frame_dif = grabber_hitbox_frames[grabber_data->ply] - throw_release_frames[thrower_data->ply];
    const bool guarenteed_timing = throw_grab_frame_dif == 0 || throw_grab_frame_dif == -1;
    const int throw_to_now_frame_dif = event_vars->game_timer - throw_release_frames[thrower_data->ply];

    if (grabber_data->state_id == ASID_CATCHWAIT) {
        //BANG. LOG OSD
        if (guarenteed_timing)
            Message_Display(OSD_Handoff, grabber_data->ply, MSGCOLOR_GREEN, "Handoff Sucessful\nPerfect Timing");
        else
            Message_Display(OSD_Handoff, grabber_data->ply, MSGCOLOR_YELLOW, "Handoff Sucessful\n%d Frame(s) Late",
                            throw_grab_frame_dif);

        throw_release_frames[thrower_data->ply] = 0;
        grabber_hitbox_frames[grabber_data->ply] = 0;
    }
    if (throw_to_now_frame_dif > 15) {
        int frames_off = throw_grab_frame_dif < 0 ? throw_grab_frame_dif + 1 : throw_grab_frame_dif;
        Message_Display(OSD_Handoff, grabber_data->ply, MSGCOLOR_RED,
                        "Handoff Failure\nIncorrect Spacing or timing. %d Frame(s) off", frames_off);
        throw_release_frames[thrower_data->ply] = 0;
        grabber_hitbox_frames[grabber_data->ply] = 0;
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
                for (int enm = 0; enm < 6; ++ennm) {
                    if (enm == ply) continue;
                    GOBJ *enm_ft = Fighter_GetSubcharGObj(enm, 0);
                    GOBJ *enm_ft_sub = Fighter_GetSubcharGObj(enm, 1);
                    RunOSD_Handoff(ft, ft_sub, enm_ft, enm_ft_sub);
                    RunOSD_Handoff(ft_sub, ft, enm_ft, enm_ft_sub);
                }
            }
        }
    }
}
