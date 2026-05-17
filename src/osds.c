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
static void RunOSD_PNJ(GOBJ *ft, GOBJ *ft_sub) {
    if (!ft || !ft_sub) return;
    const int frame_delay_required = 6;
    const int osd_window = 3;

    static int ft_turn_frame[6] = {0};
    static int ft_sub_state_previous[6] = {0};
    FighterData *ft_data = ft->userdata;
    FighterData *ft_sub_data = ft_sub->userdata;
    if (ft_data->state_id == ASID_TURN) {
        ft_turn_frame[ft_data->ply] = event_vars->game_timer;
    }
    if (ft_sub_data->state_id == ASID_KNEEBEND && ft_sub_state_previous[ft_data->ply] != ASID_KNEEBEND) {
        //a pnj happens when ft_sub enters jump 6 frames after ft enters turn
        if (event_vars->game_timer - ft_turn_frame[ft_data->ply] == frame_delay_required) {
            Message_Display(OSD_FighterSpecificTech, ft_data->ply, MSGCOLOR_GREEN, "PNJ Success");
        }
        //if you were within 3 frames on either side display a fail message. keep band small to avoid spam when dash jumping
        int diff = event_vars->game_timer - ft_turn_frame[ft_data->ply];
        if (diff >= osd_window && diff < frame_delay_required) {
            Message_Display(OSD_FighterSpecificTech, ft_data->ply, MSGCOLOR_RED, "PNJ Fail\n%dF Early", frame_delay_required - diff);
        } else if (diff > frame_delay_required && diff <= frame_delay_required + osd_window) {
            Message_Display(OSD_FighterSpecificTech, ft_data->ply, MSGCOLOR_RED, "PNJ Fail\n%dF Late", diff - frame_delay_required);
        }
    }
    ft_sub_state_previous[ft_data->ply] = ft_sub_data->state_id;
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
        if (osd_enabled && 1u << OSD_FighterSpecificTech) RunOSD_PNJ(ft, ft_sub);
    }
}
