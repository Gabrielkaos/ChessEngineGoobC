#include "defs.h"
#include "trace.h"
#include "board.h"
#include "search.h"
#include "pvtable.h"
#include "uci.h"
#include "nnue_loader.h"
#include "misc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef TRACE

static SearchTrace g_current_trace;
static SearchTrace g_cumulative_trace;
static int g_searches_count = 0;

void trace_init(void) {
    memset(&g_current_trace, 0, sizeof(SearchTrace));
    memset(&g_cumulative_trace, 0, sizeof(SearchTrace));
    g_searches_count = 0;
}

void trace_reset(void) {
    memset(&g_current_trace, 0, sizeof(SearchTrace));
    memset(&g_cumulative_trace, 0, sizeof(SearchTrace));
    g_searches_count = 0;
    printf("info string Trace statistics reset to zero\n");
}

void trace_start_search(void) {
    memset(&g_current_trace, 0, sizeof(SearchTrace));
}

#define ADD_TRACE_FIELD(dst, src, f) ((dst)->f += (src)->f)

void trace_aggregate_thread(const SearchTrace *thread_trace) {
    if (!thread_trace) return;
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, ab_nodes);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_nodes);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, eval_calls);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, tt_hits);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, tt_cutoffs);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, tt_research_cutoffs);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, syzygy_probes);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, syzygy_hits);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, hindsight_ext);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, hindsight_red);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, razoring_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, razoring_qs_called);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, razoring_cutoffs);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, beta_prune_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, beta_prune_cutoffs);


    ADD_TRACE_FIELD(&g_current_trace, thread_trace, nmp_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, nmp_direct_cutoffs);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, nmp_verification_started);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, nmp_verification_passed);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, nmp_verification_failed);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, iir_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, iir_reduced);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, probcut_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, probcut_moves_tried);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, probcut_cutoffs);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, moves_considered);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, moves_legal);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, futility_skip_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, futility_skip_pruned);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, futility_move_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, futility_move_pruned);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, lmp_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, lmp_pruned);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, countermove_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, countermove_pruned);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, followup_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, followup_pruned);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, see_quiet_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, see_quiet_pruned);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, see_noisy_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, see_noisy_pruned);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, singular_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, singular_single_ext);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, singular_double_ext);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, singular_multicut);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, singular_negative_ext);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, check_ext);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, history_ext);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, lmr_quiet_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, lmr_quiet_reduced);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, lmr_noisy_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, lmr_noisy_reduced);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, lmr_researches);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, srd_deficit_triggered);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, srd_surplus_triggered);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, srd_sibling_triggered);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, beta_cutoffs);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, cutoff_first_move);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, cutoff_tt_move);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, cutoff_killer);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, cutoff_counter);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, cutoff_followup);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, cutoff_quiet);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, cutoff_noisy);

    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_tt_hits);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_tt_cutoffs);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_stand_pat_evals);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_stand_pat_cutoffs);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_delta_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_delta_pruned);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_see_attempted);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_see_pruned);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_moves_tried);
    ADD_TRACE_FIELD(&g_current_trace, thread_trace, qs_beta_cutoffs);
}

void trace_finish_search(void) {
    trace_aggregate_thread(&g_current_trace); // Add current to cumulative
    // Wait: ADD_TRACE_FIELD in trace_aggregate_thread added to g_current_trace.
    // To add g_current_trace to g_cumulative_trace:
    SearchTrace *dst = &g_cumulative_trace;
    const SearchTrace *src = &g_current_trace;

    ADD_TRACE_FIELD(dst, src, ab_nodes);
    ADD_TRACE_FIELD(dst, src, qs_nodes);
    ADD_TRACE_FIELD(dst, src, eval_calls);
    ADD_TRACE_FIELD(dst, src, tt_hits);
    ADD_TRACE_FIELD(dst, src, tt_cutoffs);
    ADD_TRACE_FIELD(dst, src, tt_research_cutoffs);
    ADD_TRACE_FIELD(dst, src, syzygy_probes);
    ADD_TRACE_FIELD(dst, src, syzygy_hits);

    ADD_TRACE_FIELD(dst, src, hindsight_ext);
    ADD_TRACE_FIELD(dst, src, hindsight_red);

    ADD_TRACE_FIELD(dst, src, razoring_attempted);
    ADD_TRACE_FIELD(dst, src, razoring_qs_called);
    ADD_TRACE_FIELD(dst, src, razoring_cutoffs);

    ADD_TRACE_FIELD(dst, src, beta_prune_attempted);
    ADD_TRACE_FIELD(dst, src, beta_prune_cutoffs);


    ADD_TRACE_FIELD(dst, src, nmp_attempted);
    ADD_TRACE_FIELD(dst, src, nmp_direct_cutoffs);
    ADD_TRACE_FIELD(dst, src, nmp_verification_started);
    ADD_TRACE_FIELD(dst, src, nmp_verification_passed);
    ADD_TRACE_FIELD(dst, src, nmp_verification_failed);

    ADD_TRACE_FIELD(dst, src, iir_attempted);
    ADD_TRACE_FIELD(dst, src, iir_reduced);

    ADD_TRACE_FIELD(dst, src, probcut_attempted);
    ADD_TRACE_FIELD(dst, src, probcut_moves_tried);
    ADD_TRACE_FIELD(dst, src, probcut_cutoffs);

    ADD_TRACE_FIELD(dst, src, moves_considered);
    ADD_TRACE_FIELD(dst, src, moves_legal);

    ADD_TRACE_FIELD(dst, src, futility_skip_attempted);
    ADD_TRACE_FIELD(dst, src, futility_skip_pruned);

    ADD_TRACE_FIELD(dst, src, futility_move_attempted);
    ADD_TRACE_FIELD(dst, src, futility_move_pruned);

    ADD_TRACE_FIELD(dst, src, lmp_attempted);
    ADD_TRACE_FIELD(dst, src, lmp_pruned);

    ADD_TRACE_FIELD(dst, src, countermove_attempted);
    ADD_TRACE_FIELD(dst, src, countermove_pruned);

    ADD_TRACE_FIELD(dst, src, followup_attempted);
    ADD_TRACE_FIELD(dst, src, followup_pruned);

    ADD_TRACE_FIELD(dst, src, see_quiet_attempted);
    ADD_TRACE_FIELD(dst, src, see_quiet_pruned);
    ADD_TRACE_FIELD(dst, src, see_noisy_attempted);
    ADD_TRACE_FIELD(dst, src, see_noisy_pruned);

    ADD_TRACE_FIELD(dst, src, singular_attempted);
    ADD_TRACE_FIELD(dst, src, singular_single_ext);
    ADD_TRACE_FIELD(dst, src, singular_double_ext);
    ADD_TRACE_FIELD(dst, src, singular_multicut);
    ADD_TRACE_FIELD(dst, src, singular_negative_ext);

    ADD_TRACE_FIELD(dst, src, check_ext);
    ADD_TRACE_FIELD(dst, src, history_ext);

    ADD_TRACE_FIELD(dst, src, lmr_quiet_attempted);
    ADD_TRACE_FIELD(dst, src, lmr_quiet_reduced);
    ADD_TRACE_FIELD(dst, src, lmr_noisy_attempted);
    ADD_TRACE_FIELD(dst, src, lmr_noisy_reduced);
    ADD_TRACE_FIELD(dst, src, lmr_researches);

    ADD_TRACE_FIELD(dst, src, srd_deficit_triggered);
    ADD_TRACE_FIELD(dst, src, srd_surplus_triggered);
    ADD_TRACE_FIELD(dst, src, srd_sibling_triggered);

    ADD_TRACE_FIELD(dst, src, beta_cutoffs);
    ADD_TRACE_FIELD(dst, src, cutoff_first_move);
    ADD_TRACE_FIELD(dst, src, cutoff_tt_move);
    ADD_TRACE_FIELD(dst, src, cutoff_killer);
    ADD_TRACE_FIELD(dst, src, cutoff_counter);
    ADD_TRACE_FIELD(dst, src, cutoff_followup);
    ADD_TRACE_FIELD(dst, src, cutoff_quiet);
    ADD_TRACE_FIELD(dst, src, cutoff_noisy);

    ADD_TRACE_FIELD(dst, src, qs_tt_hits);
    ADD_TRACE_FIELD(dst, src, qs_tt_cutoffs);
    ADD_TRACE_FIELD(dst, src, qs_stand_pat_evals);
    ADD_TRACE_FIELD(dst, src, qs_stand_pat_cutoffs);
    ADD_TRACE_FIELD(dst, src, qs_delta_attempted);
    ADD_TRACE_FIELD(dst, src, qs_delta_pruned);
    ADD_TRACE_FIELD(dst, src, qs_see_attempted);
    ADD_TRACE_FIELD(dst, src, qs_see_pruned);
    ADD_TRACE_FIELD(dst, src, qs_moves_tried);
    ADD_TRACE_FIELD(dst, src, qs_beta_cutoffs);

    g_searches_count++;
}

static inline double rate(uint64_t num, uint64_t den) {
    return den > 0 ? ((double)num * 100.0 / (double)den) : 0.0;
}

void trace_print(int current_only) {
    const SearchTrace *t = current_only ? &g_current_trace : &g_cumulative_trace;
    uint64_t total_nodes = t->ab_nodes + t->qs_nodes;

    printf("\n========================================================================================\n");
    printf("                  GOOB SEARCH HEURISTIC TRACE REPORT (%s)\n", current_only ? "LAST SEARCH" : "CUMULATIVE");
    if (!current_only) {
        printf("  Searches Recorded: %d\n", g_searches_count);
    }
    printf("========================================================================================\n");
    printf("  Nodes Visited:   AB Nodes = %" PRIu64 " | QS Nodes = %" PRIu64 " | Total = %" PRIu64 "\n",
           t->ab_nodes, t->qs_nodes, total_nodes);
    printf("  Static Evals:    Calls = %" PRIu64 "\n", t->eval_calls);
    printf("  TT Probes:       Hits = %" PRIu64 " (%.2f%% of AB) | Cutoffs = %" PRIu64 " (%.2f%% of hits)\n",
           t->tt_hits, rate(t->tt_hits, t->ab_nodes),
           t->tt_cutoffs, rate(t->tt_cutoffs, t->tt_hits));
    if (t->syzygy_probes > 0) {
        printf("  Syzygy Probes:   Probes = %" PRIu64 " | Hits = %" PRIu64 " (%.2f%%)\n",
               t->syzygy_probes, t->syzygy_hits, rate(t->syzygy_hits, t->syzygy_probes));
    }

    printf("\n--- INTERIOR-NODE PRUNINGS & REDUCTIONS ---\n");
    printf("  %-25s %12s %12s %9s   %-30s\n", "Heuristic", "Considered", "Triggered", "Rate (%)", "Governing Constants");
    printf("  --------------------------------------------------------------------------------------\n");
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   BetaPruningDepth(%d), BetaMargin(%d)\n",
           "Beta Pruning (RFP)", t->beta_prune_attempted, t->beta_prune_cutoffs,
           rate(t->beta_prune_cutoffs, t->beta_prune_attempted),
           BetaPruningDepth, BetaMargin);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   defaultNullMoveDepth(%d)\n",
           "Null Move Attempted", t->nmp_attempted,
           t->nmp_direct_cutoffs + t->nmp_verification_passed,
           rate(t->nmp_direct_cutoffs + t->nmp_verification_passed, t->nmp_attempted),
           defaultNullMoveDepth);
    printf("    -> Direct Cutoff       %12s %12" PRIu64 " %8.2f%%   depth < NMPVerifyDepth(%d)\n",
           "", t->nmp_direct_cutoffs, rate(t->nmp_direct_cutoffs, t->nmp_attempted),
           NMPVerifyDepth);
    printf("    -> Verification        %12" PRIu64 " %12" PRIu64 " %8.2f%%   pass/(pass+fail)\n",
           t->nmp_verification_started, t->nmp_verification_passed,
           rate(t->nmp_verification_passed, t->nmp_verification_passed + t->nmp_verification_failed));
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   RazoringDepth(%d), Base(%d), Coeff(%d)\n",
           "Razoring (Cutoff)", t->razoring_attempted, t->razoring_cutoffs,
           rate(t->razoring_cutoffs, t->razoring_attempted),
           RazoringDepth, RazorMarginBase, RazorMarginCoeff);
    printf("    -> QS Verification     %12" PRIu64 " %12" PRIu64 " %8.2f%%   eval < alpha - margin\n",
           t->razoring_attempted, t->razoring_qs_called,
           rate(t->razoring_qs_called, t->razoring_attempted));
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   probCutDepth(%d), probCutMargin(%d)\n",
           "ProbCut", t->probcut_attempted, t->probcut_cutoffs,
           rate(t->probcut_cutoffs, t->probcut_attempted),
           probCutDepth, probCutMargin);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   IIRDepth(%d)\n",
           "IIR Reductions", t->iir_attempted, t->iir_reduced,
           rate(t->iir_reduced, t->iir_attempted),
           IIRDepth);
    printf("  %-25s %12s %12" PRIu64 " %9s   priorReduction >= 3\n",
           "Hindsight Extensions", "", t->hindsight_ext, "");
    printf("  %-25s %12s %12" PRIu64 " %9s   HindsightMargin(%d)\n",
           "Hindsight Reductions", "", t->hindsight_red, "", HindsightMargin);
    printf("  %-25s %12s %12" PRIu64 " %9s   TTResearchMargin(%d)\n",
           "TT Research Cutoff", "", t->tt_research_cutoffs, "", TTResearchMargin);

    printf("\n--- MOVE LOOP PRUNINGS ---\n");
    printf("  %-25s %12s %12s %9s   %-30s\n", "Heuristic", "Considered", "Triggered", "Rate (%)", "Governing Constants");
    printf("  --------------------------------------------------------------------------------------\n");
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   FutilityMargin(%d), NoHist(%d), Depth(%d)\n",
           "Futility (skipQuiets)", t->futility_skip_attempted, t->futility_skip_pruned,
           rate(t->futility_skip_pruned, t->futility_skip_attempted),
           FutilityMargin, FutilityMarginNoHistory, FutilityPruningDepth);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   FutilityMargin(%d), HistoryLimit\n",
           "Futility (per-move)", t->futility_move_attempted, t->futility_move_pruned,
           rate(t->futility_move_pruned, t->futility_move_attempted),
           FutilityMargin);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   LateMovePruningDepth(%d), Counts Table\n",
           "Late Move Pruning", t->lmp_attempted, t->lmp_pruned,
           rate(t->lmp_pruned, t->lmp_attempted),
           LateMovePruningDepth);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   CounterMovePruningDepth[%d,%d]\n",
           "CounterMove Pruning", t->countermove_attempted, t->countermove_pruned,
           rate(t->countermove_pruned, t->countermove_attempted),
           CounterMovePruningDepth[0], CounterMovePruningDepth[1]);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   FollowUpMovePruningDepth[%d,%d]\n",
           "FollowUpMove Pruning", t->followup_attempted, t->followup_pruned,
           rate(t->followup_pruned, t->followup_attempted),
           FollowUpMovePruningDepth[0], FollowUpMovePruningDepth[1]);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   SEEPruningDepth(%d), SEEQuietMargin(%d)\n",
           "SEE Quiet Pruning", t->see_quiet_attempted, t->see_quiet_pruned,
           rate(t->see_quiet_pruned, t->see_quiet_attempted),
           SEEPruningDepth, SEEQuietMargin);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   SEEPruningDepth(%d), SEENoisyMargin(%d)\n",
           "SEE Noisy Pruning", t->see_noisy_attempted, t->see_noisy_pruned,
           rate(t->see_noisy_pruned, t->see_noisy_attempted),
           SEEPruningDepth, SEENoisyMargin);

    printf("\n--- EXTENSIONS & REDUCTIONS ---\n");
    printf("  %-25s %12s %12s %9s   %-30s\n", "Heuristic", "Considered", "Triggered", "Rate (%)", "Governing Constants");
    printf("  --------------------------------------------------------------------------------------\n");
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   depth >= 7, ttMove, HFBETA\n",
           "Singular Search", t->singular_attempted,
           t->singular_single_ext + t->singular_double_ext + t->singular_multicut,
           rate(t->singular_single_ext + t->singular_double_ext + t->singular_multicut, t->singular_attempted));
    printf("    -> Single Extension    %12s %12" PRIu64 " %8.2f%%   value <= rBeta\n",
           "", t->singular_single_ext, rate(t->singular_single_ext, t->singular_attempted));
    printf("    -> Double Extension    %12s %12" PRIu64 " %8.2f%%   DoubleExtMargin(%d)\n",
           "", t->singular_double_ext, rate(t->singular_double_ext, t->singular_attempted),
           DoubleExtMargin);
    printf("    -> MultiCut            %12s %12" PRIu64 " %8.2f%%   value > rBeta >= beta\n",
           "", t->singular_multicut, rate(t->singular_multicut, t->singular_attempted));
    printf("    -> Negative Extension  %12s %12" PRIu64 " %8.2f%%   depth -= 3\n",
           "", t->singular_negative_ext, rate(t->singular_negative_ext, t->singular_attempted));
    printf("  %-25s %12s %12" PRIu64 " %9s   inCheck\n",
           "Check Extension", "", t->check_ext, "");
    printf("  %-25s %12s %12" PRIu64 " %9s   HistexLimit(%d)\n",
           "History Extension", "", t->history_ext, "", HistexLimit);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   LMRTable[depth][legal], AllNodeScale\n",
           "Quiet LMR", t->lmr_quiet_attempted, t->lmr_quiet_reduced,
           rate(t->lmr_quiet_reduced, t->lmr_quiet_attempted));
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   LMRTable[depth][legal]\n",
           "Noisy LMR", t->lmr_noisy_attempted, t->lmr_noisy_reduced,
           rate(t->lmr_noisy_reduced, t->lmr_noisy_attempted));
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   Score > alpha (fail-high re-search)\n",
           "LMR Re-searches", t->lmr_quiet_reduced + t->lmr_noisy_reduced, t->lmr_researches,
           rate(t->lmr_researches, t->lmr_quiet_reduced + t->lmr_noisy_reduced));
    printf("  %-25s %12s %12" PRIu64 " %9s   EVAL_DEFICIT_MARGIN(%d)\n",
           "Surprise-SRD Deficit", "", t->srd_deficit_triggered, "", EVAL_DEFICIT_MARGIN);
    printf("  %-25s %12s %12" PRIu64 " %9s   EVAL_SURPLUS_MARGIN(%d)\n",
           "Surprise-SRD Surplus", "", t->srd_surplus_triggered, "", EVAL_SURPLUS_MARGIN);
    printf("  %-25s %12s %12" PRIu64 " %9s   Sibling Surprise (R -= 1)\n",
           "Surprise-SRD Sibling", "", t->srd_sibling_triggered, "");

    printf("\n--- QUIESCENCE SEARCH ---\n");
    printf("  %-25s %12s %12s %9s   %-30s\n", "Heuristic", "Considered", "Triggered", "Rate (%)", "Governing Constants");
    printf("  --------------------------------------------------------------------------------------\n");
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   eval >= beta\n",
           "QS Stand-pat Cutoff", t->qs_stand_pat_evals, t->qs_stand_pat_cutoffs,
           rate(t->qs_stand_pat_cutoffs, t->qs_stand_pat_evals));
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   DeltaMarginQ(%d)\n",
           "QS Delta Pruning", t->qs_delta_attempted, t->qs_delta_pruned,
           rate(t->qs_delta_pruned, t->qs_delta_attempted),
           DeltaMarginQ);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   QSSeeMargin(%d)\n",
           "QS SEE Pruning", t->qs_see_attempted, t->qs_see_pruned,
           rate(t->qs_see_pruned, t->qs_see_attempted),
           QSSeeMargin);
    printf("  %-25s %12" PRIu64 " %12" PRIu64 " %8.2f%%   ttDepth >= 0\n",
           "QS TT Cutoffs", t->qs_tt_hits, t->qs_tt_cutoffs,
           rate(t->qs_tt_cutoffs, t->qs_tt_hits));

    printf("\n--- CUTOFF DISTRIBUTION & MOVE ORDERING ---\n");
    printf("  Total Beta Cutoffs (Fail-Highs): %" PRIu64 "\n", t->beta_cutoffs);
    printf("    -> 1st Move Cutoff:            %" PRIu64 " (%6.2f%%)\n",
           t->cutoff_first_move, rate(t->cutoff_first_move, t->beta_cutoffs));
    printf("    -> TT Move Cutoff:             %" PRIu64 " (%6.2f%%)\n",
           t->cutoff_tt_move, rate(t->cutoff_tt_move, t->beta_cutoffs));
    printf("    -> Killer Move Cutoff:         %" PRIu64 " (%6.2f%%)\n",
           t->cutoff_killer, rate(t->cutoff_killer, t->beta_cutoffs));
    printf("    -> Counter Move Cutoff:        %" PRIu64 " (%6.2f%%)\n",
           t->cutoff_counter, rate(t->cutoff_counter, t->beta_cutoffs));
    printf("    -> Followup Move Cutoff:       %" PRIu64 " (%6.2f%%)\n",
           t->cutoff_followup, rate(t->cutoff_followup, t->beta_cutoffs));
    printf("    -> Other Quiet Move Cutoff:    %" PRIu64 " (%6.2f%%)\n",
           t->cutoff_quiet, rate(t->cutoff_quiet, t->beta_cutoffs));
    printf("    -> Tactical/Noisy Cutoff:      %" PRIu64 " (%6.2f%%)\n",
           t->cutoff_noisy, rate(t->cutoff_noisy, t->beta_cutoffs));
    printf("========================================================================================\n\n");
    fflush(stdout);
}

void trace_print_json(int current_only) {
    const SearchTrace *t = current_only ? &g_current_trace : &g_cumulative_trace;
    uint64_t total_nodes = t->ab_nodes + t->qs_nodes;

    printf("{\n");
    printf("  \"trace_enabled\": true,\n");
    printf("  \"mode\": \"%s\",\n", current_only ? "current" : "cumulative");
    printf("  \"searches_count\": %d,\n", current_only ? 1 : g_searches_count);
    printf("  \"ab_nodes\": %" PRIu64 ",\n", t->ab_nodes);
    printf("  \"qs_nodes\": %" PRIu64 ",\n", t->qs_nodes);
    printf("  \"total_nodes\": %" PRIu64 ",\n", total_nodes);
    printf("  \"eval_calls\": %" PRIu64 ",\n", t->eval_calls);
    printf("  \"tt_hits\": %" PRIu64 ",\n", t->tt_hits);
    printf("  \"tt_cutoffs\": %" PRIu64 ",\n", t->tt_cutoffs);
    printf("  \"tt_research_cutoffs\": %" PRIu64 ",\n", t->tt_research_cutoffs);
    printf("  \"syzygy_probes\": %" PRIu64 ",\n", t->syzygy_probes);
    printf("  \"syzygy_hits\": %" PRIu64 ",\n", t->syzygy_hits);

    printf("  \"heuristics\": {\n");
    printf("    \"beta_pruning\": {\"considered\": %" PRIu64 ", \"triggered\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->beta_prune_attempted, t->beta_prune_cutoffs, rate(t->beta_prune_cutoffs, t->beta_prune_attempted) / 100.0);
    printf("    \"nmp\": {\"attempted\": %" PRIu64 ", \"direct_cutoffs\": %" PRIu64 ", \"verification_started\": %" PRIu64 ", \"verification_passed\": %" PRIu64 ", \"verification_failed\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->nmp_attempted, t->nmp_direct_cutoffs, t->nmp_verification_started, t->nmp_verification_passed, t->nmp_verification_failed,
           rate(t->nmp_direct_cutoffs + t->nmp_verification_passed, t->nmp_attempted) / 100.0);
    printf("    \"razoring\": {\"attempted\": %" PRIu64 ", \"qs_called\": %" PRIu64 ", \"cutoffs\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->razoring_attempted, t->razoring_qs_called, t->razoring_cutoffs, rate(t->razoring_cutoffs, t->razoring_attempted) / 100.0);
    printf("    \"probcut\": {\"attempted\": %" PRIu64 ", \"moves_tried\": %" PRIu64 ", \"cutoffs\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->probcut_attempted, t->probcut_moves_tried, t->probcut_cutoffs, rate(t->probcut_cutoffs, t->probcut_attempted) / 100.0);
    printf("    \"iir\": {\"attempted\": %" PRIu64 ", \"reduced\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->iir_attempted, t->iir_reduced, rate(t->iir_reduced, t->iir_attempted) / 100.0);
    printf("    \"hindsight\": {\"extensions\": %" PRIu64 ", \"reductions\": %" PRIu64 "},\n",
           t->hindsight_ext, t->hindsight_red);
    printf("    \"futility_skip\": {\"considered\": %" PRIu64 ", \"triggered\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->futility_skip_attempted, t->futility_skip_pruned, rate(t->futility_skip_pruned, t->futility_skip_attempted) / 100.0);
    printf("    \"futility_move\": {\"considered\": %" PRIu64 ", \"triggered\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->futility_move_attempted, t->futility_move_pruned, rate(t->futility_move_pruned, t->futility_move_attempted) / 100.0);
    printf("    \"lmp\": {\"considered\": %" PRIu64 ", \"triggered\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->lmp_attempted, t->lmp_pruned, rate(t->lmp_pruned, t->lmp_attempted) / 100.0);
    printf("    \"countermove_prune\": {\"considered\": %" PRIu64 ", \"triggered\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->countermove_attempted, t->countermove_pruned, rate(t->countermove_pruned, t->countermove_attempted) / 100.0);
    printf("    \"followup_prune\": {\"considered\": %" PRIu64 ", \"triggered\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->followup_attempted, t->followup_pruned, rate(t->followup_pruned, t->followup_attempted) / 100.0);
    printf("    \"see_quiet\": {\"considered\": %" PRIu64 ", \"triggered\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->see_quiet_attempted, t->see_quiet_pruned, rate(t->see_quiet_pruned, t->see_quiet_attempted) / 100.0);
    printf("    \"see_noisy\": {\"considered\": %" PRIu64 ", \"triggered\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->see_noisy_attempted, t->see_noisy_pruned, rate(t->see_noisy_pruned, t->see_noisy_attempted) / 100.0);
    printf("    \"singular\": {\"attempted\": %" PRIu64 ", \"single_ext\": %" PRIu64 ", \"double_ext\": %" PRIu64 ", \"multicut\": %" PRIu64 ", \"negative_ext\": %" PRIu64 "},\n",
           t->singular_attempted, t->singular_single_ext, t->singular_double_ext, t->singular_multicut, t->singular_negative_ext);
    printf("    \"check_ext\": %" PRIu64 ",\n", t->check_ext);
    printf("    \"history_ext\": %" PRIu64 ",\n", t->history_ext);
    printf("    \"lmr_quiet\": {\"considered\": %" PRIu64 ", \"reduced\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->lmr_quiet_attempted, t->lmr_quiet_reduced, rate(t->lmr_quiet_reduced, t->lmr_quiet_attempted) / 100.0);
    printf("    \"lmr_noisy\": {\"considered\": %" PRIu64 ", \"reduced\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->lmr_noisy_attempted, t->lmr_noisy_reduced, rate(t->lmr_noisy_reduced, t->lmr_noisy_attempted) / 100.0);
    printf("    \"lmr_researches\": {\"reduced_searches\": %" PRIu64 ", \"researched\": %" PRIu64 ", \"rate\": %.4f},\n",
           t->lmr_quiet_reduced + t->lmr_noisy_reduced, t->lmr_researches,
           rate(t->lmr_researches, t->lmr_quiet_reduced + t->lmr_noisy_reduced) / 100.0);
    printf("    \"srd\": {\"deficit\": %" PRIu64 ", \"surplus\": %" PRIu64 ", \"sibling\": %" PRIu64 "},\n",
           t->srd_deficit_triggered, t->srd_surplus_triggered, t->srd_sibling_triggered);
    printf("    \"qs\": {\"stand_pat_evals\": %" PRIu64 ", \"stand_pat_cutoffs\": %" PRIu64 ", \"delta_attempted\": %" PRIu64 ", \"delta_pruned\": %" PRIu64 ", \"see_attempted\": %" PRIu64 ", \"see_pruned\": %" PRIu64 "}\n",
           t->qs_stand_pat_evals, t->qs_stand_pat_cutoffs, t->qs_delta_attempted, t->qs_delta_pruned, t->qs_see_attempted, t->qs_see_pruned);
    printf("  },\n");

    printf("  \"cutoffs\": {\n");
    printf("    \"total\": %" PRIu64 ",\n", t->beta_cutoffs);
    printf("    \"first_move\": %" PRIu64 ",\n", t->cutoff_first_move);
    printf("    \"tt_move\": %" PRIu64 ",\n", t->cutoff_tt_move);
    printf("    \"killer\": %" PRIu64 ",\n", t->cutoff_killer);
    printf("    \"counter\": %" PRIu64 ",\n", t->cutoff_counter);
    printf("    \"followup\": %" PRIu64 ",\n", t->cutoff_followup);
    printf("    \"quiet\": %" PRIu64 ",\n", t->cutoff_quiet);
    printf("    \"noisy\": %" PRIu64 "\n", t->cutoff_noisy);
    printf("  }\n");
    printf("}\n");
    fflush(stdout);
}

#else // !TRACE

void trace_init(void) {}
void trace_reset(void) {
    printf("info string TRACE is disabled in this build. Recompile with 'make trace' or 'make TRACE=1'\n");
}
void trace_start_search(void) {}
void trace_aggregate_thread(const SearchTrace *thread_trace) { (void)thread_trace; }
void trace_finish_search(void) {}
void trace_print(int current_only) {
    (void)current_only;
    printf("\n========================================================================================\n");
    printf("  TRACE NOT ENABLED AT COMPILE TIME\n");
    printf("  Please recompile with:  make trace   (or make TRACE=1 native)\n");
    printf("========================================================================================\n\n");
    fflush(stdout);
}
void trace_print_json(int current_only) {
    (void)current_only;
    printf("{\"trace_enabled\": false, \"error\": \"Engine compiled without -DTRACE. Rebuild with 'make trace'.\"}\n");
    fflush(stdout);
}

#endif // TRACE

// Standard benchmark positions covering openings, middlegames, endgames, tactical, and quiet structures
static const char *bench_fens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "r2q1rk1/pbp1bppp/1pn1pn2/3p4/2PP4/1PN1PN2/PB2BPPP/R2Q1RK1 b - - 0 9",
    "r1bq1rk1/pp2bppp/2n1pn2/2pp4/2PP4/2N1PN2/PP2BPPP/R1BQ1RK1 w - - 0 8",
    "r1b2rk1/1pq1bppp/p1nppn2/8/2PNP3/1PN5/P3BPPP/R1BQ1RK1 w - - 0 11",
    "r2qkb1r/pp2pppp/2n2n2/3p4/3P2b1/3B1N2/PPP2PPP/RNBQK2R w KQkq - 0 7",
    "r1b1k2r/pppp1ppp/2n5/4p3/2B1n3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 6",
    "r1bqk2r/pppp1ppp/2n2n2/4p3/1bB1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 0 5",
    "r3k2r/2pb1ppp/2pp1q2/p7/1nP1B3/1P2P3/P2N1PPP/R2QK2R w KQkq a6 0 14",
    "4rrk1/2p1b1p1/p1p3q1/4p3/2P2n1p/1P1NR2P/PB3PP1/3R1QK1 b - - 2 24",
    "r3qbrk/6p1/2b2pPp/p3pP1Q/PpPpP2P/3P1B2/2PB3K/R5R1 w - - 16 42",
    "6k1/1R3p2/6p1/2Bp3p/3P2q1/P7/1P2rQ1K/5R2 b - - 4 44",
    "8/8/1p2k1p1/3p3p/1p1P1P1P/1P2PK2/8/8 w - - 3 54",
    "7r/2p3k1/1p1p1qp1/1P1Bp3/p1P2r1P/P7/4R3/Q4RK1 w - - 0 36",
    "r1bq1rk1/pp2b1pp/n1pp1n2/3P1p2/2P1p3/2N1P2N/PP2BPPP/R1BQ1RK1 b - - 2 10",
    "3r3k/2r4p/1p1b3q/p4P2/P2Pp3/1B2P3/3BQ1RP/6K1 w - - 3 87",
    "2r4r/1p4k1/1Pnp4/3Qb1pq/8/4BpPp/5P2/2RR1BK1 w - - 0 42",
    "4q1bk/6b1/7p/p1p4p/PNPpP2P/KN4P1/3Q4/4R3 b - - 0 37",
    "2q3r1/1r2pk2/pp3pp1/2pP3p/P1Pb1BbP/1P4Q1/R3NPP1/4R1K1 w - - 2 34",
    "1r2r2k/1b4q1/pp5p/2pPp1p1/P3Pn2/1P1B1Q1P/2R3P1/4BR1K b - - 1 37",
    "r3kbbr/pp1n1p1P/3ppnp1/q5N1/1P1pP3/P1N1B3/2P1QP2/R3KB1R b KQkq b3 0 17",
    "8/6pk/2b1Rp2/3r4/1R1B2PP/P5K1/8/2r5 b - - 16 42",
    "1r4k1/4ppb1/2n1b1qp/pB4p1/1n1BP1P1/7P/2PNQPK1/3RN3 w - - 8 29",
    "8/p2B4/PkP5/4p1pK/4Pb1p/5P2/8/8 w - - 29 68",
    "3r4/ppq1ppkp/4bnp1/2pN4/2P1P3/1P4P1/PQ3PBP/R4K2 b - - 2 20",
    "5rr1/4n2k/4q2P/P1P2n2/3B1p2/4pP2/2N1P3/1RR1K2Q w - - 1 49",
    "1r5k/2pq2p1/3p3p/p1pP4/4QP2/PP1R3P/6PK/8 w - - 1 51",
    "q5k1/5ppp/1r3bn1/1B6/P1N2P2/BQ2P1P1/5K1P/8 b - - 2 34"
};

#define BENCH_FENS_COUNT ((int)(sizeof(bench_fens)/sizeof(bench_fens[0])))

void run_bench(S_BOARD *pos, S_SEARCHINFO *info, S_PVTABLE *table, int depth) {
    if (depth <= 0) depth = 8;
    printf("\nRunning benchmark on %d positions to depth %d...\n\n", BENCH_FENS_COUNT, depth);

#ifdef TRACE
    trace_reset();
#endif

    U64 total_nodes = 0;
    int start_time = getTimeMs();

    for (int i = 0; i < BENCH_FENS_COUNT; ++i) {
        char fenBuf[256];
        snprintf(fenBuf, sizeof(fenBuf), "position fen %s\n", bench_fens[i]);
        parsePosition(fenBuf, pos);

        info->starttime = getTimeMs();
        info->depth = depth;
        info->depthSet = TRUE;
        info->timeSet = FALSE;
        info->softTimeSet = FALSE;
        info->nodeSet = FALSE;
        info->UciInfinite = FALSE;
        info->ponder = FALSE;
        info->stopped = FALSE;
        info->depthOneComplete = FALSE;

        int t0 = getTimeMs();

        SearchPosition(pos, info, table);

        int t_elapsed = getTimeMs() - t0;
        U64 pos_nodes = info->nodes;
        total_nodes += pos_nodes;

        printf("Position [%2d/%d]: nodes %10" PRIu64 "  time %6d ms\n",
               i + 1, BENCH_FENS_COUNT, pos_nodes, t_elapsed);
        fflush(stdout);
    }

    int total_time = getTimeMs() - start_time;
    if (total_time <= 0) total_time = 1;
    uint64_t nps = (total_nodes * 1000ULL) / (uint64_t)total_time;

    printf("\n========================================================================================\n");
    printf("Benchmark Summary: %d positions | Total Nodes: %" PRIu64 " | Total Time: %d ms | NPS: %" PRIu64 "\n",
           BENCH_FENS_COUNT, total_nodes, total_time, nps);
    printf("========================================================================================\n");
    fflush(stdout);

#ifdef TRACE
    trace_print(0);
#endif
}

void handle_trace_command(const char *cmd, S_BOARD *pos, S_SEARCHINFO *info) {
    while (*cmd == ' ') cmd++;

    if (!strncmp(cmd, "trace reset", 11)) {
        trace_reset();
    } else if (!strncmp(cmd, "trace current json", 18)) {
        trace_print_json(1);
    } else if (!strncmp(cmd, "trace json", 10)) {
        trace_print_json(0);
    } else if (!strncmp(cmd, "trace current", 13)) {
        trace_print(1);
    } else if (!strncmp(cmd, "trace bench", 11)) {
        int depth = 8;
        sscanf(cmd, "trace bench %d", &depth);
        run_bench(pos, info, pvTable, depth);
    } else if (!strncmp(cmd, "trace help", 10)) {
        printf("Available trace commands:\n");
        printf("  trace              - Show cumulative heuristic trace statistics\n");
        printf("  trace current      - Show trace statistics for the last search\n");
        printf("  trace reset        - Reset all trace statistics counters to zero\n");
        printf("  trace json         - Dump cumulative trace statistics in JSON format\n");
        printf("  trace current json - Dump last search trace statistics in JSON format\n");
        printf("  trace bench [d]    - Run %d standard benchmark positions to depth d (default 8)\n", BENCH_FENS_COUNT);
        printf("  trace help         - Display this help message\n");
        fflush(stdout);
    } else {
        // Default "trace"
        trace_print(0);
    }
}
