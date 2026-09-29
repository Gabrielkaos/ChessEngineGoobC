#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include "defs.h"

// Forward declaration
typedef struct S_BOARD S_BOARD;

typedef struct {
    // General search counts
    uint64_t ab_nodes;
    uint64_t qs_nodes;
    uint64_t eval_calls;
    uint64_t tt_hits;
    uint64_t tt_cutoffs;
    uint64_t tt_research_cutoffs;
    uint64_t syzygy_probes;
    uint64_t syzygy_hits;

    // Interior-node pruning & reductions
    uint64_t hindsight_ext;
    uint64_t hindsight_red;

    uint64_t razoring_attempted;
    uint64_t razoring_qs_called;
    uint64_t razoring_cutoffs;

    uint64_t beta_prune_attempted;
    uint64_t beta_prune_cutoffs;

    uint64_t alpha_prune_attempted;
    uint64_t alpha_prune_cutoffs;

    uint64_t nmp_attempted;
    uint64_t nmp_direct_cutoffs;
    uint64_t nmp_verification_started;
    uint64_t nmp_verification_passed;
    uint64_t nmp_verification_failed;

    uint64_t iir_attempted;
    uint64_t iir_reduced;

    uint64_t probcut_attempted;
    uint64_t probcut_moves_tried;
    uint64_t probcut_cutoffs;

    // Move loop prunings
    uint64_t moves_considered;
    uint64_t moves_legal;

    uint64_t futility_skip_attempted;
    uint64_t futility_skip_pruned;

    uint64_t futility_move_attempted;
    uint64_t futility_move_pruned;

    uint64_t lmp_attempted;
    uint64_t lmp_pruned;

    uint64_t countermove_attempted;
    uint64_t countermove_pruned;

    uint64_t followup_attempted;
    uint64_t followup_pruned;

    uint64_t see_quiet_attempted;
    uint64_t see_quiet_pruned;
    uint64_t see_noisy_attempted;
    uint64_t see_noisy_pruned;

    // Extensions & Reductions
    uint64_t singular_attempted;
    uint64_t singular_single_ext;
    uint64_t singular_double_ext;
    uint64_t singular_multicut;
    uint64_t singular_negative_ext;

    uint64_t check_ext;
    uint64_t history_ext;

    uint64_t lmr_quiet_attempted;
    uint64_t lmr_quiet_reduced;
    uint64_t lmr_noisy_attempted;
    uint64_t lmr_noisy_reduced;
    uint64_t lmr_researches;

    // Surprise-SRD
    uint64_t srd_deficit_triggered;
    uint64_t srd_surplus_triggered;
    uint64_t srd_sibling_triggered;

    // Fail highs & Move ordering
    uint64_t beta_cutoffs;
    uint64_t cutoff_first_move;
    uint64_t cutoff_tt_move;
    uint64_t cutoff_killer;
    uint64_t cutoff_counter;
    uint64_t cutoff_followup;
    uint64_t cutoff_quiet;
    uint64_t cutoff_noisy;

    // Quiescence Search
    uint64_t qs_tt_hits;
    uint64_t qs_tt_cutoffs;
    uint64_t qs_stand_pat_evals;
    uint64_t qs_stand_pat_cutoffs;
    uint64_t qs_delta_attempted;
    uint64_t qs_delta_pruned;
    uint64_t qs_see_attempted;
    uint64_t qs_see_pruned;
    uint64_t qs_moves_tried;
    uint64_t qs_beta_cutoffs;
} SearchTrace;

#ifdef TRACE
    #define TRACE_INC(pos, field)      do { if ((pos) && (pos)->search) (pos)->search->trace.field++; } while(0)
    #define TRACE_ADD(pos, field, val) do { if ((pos) && (pos)->search) (pos)->search->trace.field += (val); } while(0)
#else
    #define TRACE_INC(pos, field)      ((void)0)
    #define TRACE_ADD(pos, field, val) ((void)0)
#endif

// Lifecycle and reporting functions
void trace_init(void);
void trace_start_search(void);
void trace_aggregate_thread(const SearchTrace *thread_trace);
void trace_finish_search(void);
void trace_reset(void);
void trace_print(int current_only);
void trace_print_json(int current_only);
void handle_trace_command(const char *cmd, S_BOARD *pos, S_SEARCHINFO *info);
void run_bench(S_BOARD *pos, S_SEARCHINFO *info, S_PVTABLE *table, int depth);

#endif // TRACE_H
