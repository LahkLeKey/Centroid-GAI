/** @file evolve_report.c @brief Reviewable source lineage and measured acceptance. */
#include "build_config.h"
#include "evolve_run.h"
#include <inttypes.h>
#include <string.h>

static int write_event(FILE *file, const cgai_life_collision_event *event);

int evolve_report_start(evolve_run *run) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    if (!evolve_path_join(path, sizeof(path), run->output, "run.tsv"))
        return 0;
    run->report = fopen(path, "wbx");
    if (run->report == NULL)
        return 0;
    return fprintf(run->report,
                   "LIFE_SOURCE_EVOLUTION\t4\nchecksum_kind\tFNV1A64\ninputs_checksum\t%" PRIu64
                   "\nbuild_recipe_checksum\t%" PRIu64 "\ninitial_source_checksum\t%" PRIu64
                   "\nsource_root\t%s\ncmake\t%s\nctest\t%s"
                   "\nrequested_seed\t%u\nproposal_budget\t%u\ngeneration_budget\t%u"
                   "\nrecipe_source\t%s"
                   "\npredecessor_bundle_hash\t%" PRIu64
                   "\nresume_bundle\t%s\nrestored_search_generations\t%u"
                   "\nrestored_proposals\t%u\nrestored_receipts\t%u"
                   "\nsource_context_checksum\t%" PRIu64 "\nllm_context_checksum\t%" PRIu64
                   "\nexternal_input_checksum\t%" PRIu64 "\ninitial_memory_checksum\t%" PRIu64
                   "\nprepared_memory_checksum\t%" PRIu64
                   "\npreparation_generations\t%u\nfinal_activity_generations\t%u"
                   "\nchoice_label\ttrain_loss_difference_clipped_minus_one_to_one"
                   "\ndomain_site_recipe\tauthentic_contact_frontier_v1"
                   "\ndomain_sites_depend_on_cell_toggles\t0"
                   "\nchoice_failure_policy\tdefer_all_failed_native_stages"
                   "\nheld_out_labels_in_gradients\t0\n",
                   run->inputs_checksum, run->build_recipe_checksum, run->initial_parent_checksum,
                   EVOLVE_SOURCE_DIRECTORY, EVOLVE_CMAKE_PROGRAM, EVOLVE_CTEST_PROGRAM,
                   run->options.seed, run->options.candidates, run->options.generations,
                   run->options.resume != NULL   ? "resumed_search_bundle"
                   : run->options.memory == NULL ? "fresh_default"
                                                 : "loaded_checkpoint",
                   run->options.resume != NULL ? run->checkpoint_hash : run->predecessor_hash,
                   run->options.resume == NULL ? "none" : run->options.resume,
                   run->completed_generations, run->proposals, run->ledger_count,
                   run->source_context_checksum, run->llm_context_checksum, run->external_checksum,
                   run->initial_memory_checksum, run->prepared_memory_checksum,
                   run->options.resume == NULL ? EVOLVE_CONTEXT_PREPARATION_GENERATIONS : 0U,
                   EVOLVE_CONTEXT_ACTIVITY_GENERATIONS) >= 0 &&
           fflush(run->report) == 0;
}

int evolve_report_baseline(evolve_run *run) {
    return fprintf(run->report,
                   "baseline_train\t%.17g\t%" PRIu64 "\t%" PRIu64 "\n"
                   "baseline\t%.17g\t%" PRIu64 "\t%.17g\t%" PRIu64 "\n"
                   "current_train\t%.17g\t%" PRIu64 "\t%" PRIu64 "\n"
                   "current\t%.17g\t%" PRIu64 "\t%.17g\t%" PRIu64 "\n"
                   "prepared_context_generation\t%u\ninitial_choice_version\t%" PRIu64 "\n"
                   "candidate_columns\tindex\tsource\tparent_checksum\tcandidate_checksum"
                   "\tgeneration\tconflict\toutput\tedits\tstatus\tdev_loss\tconfirm_loss"
                   "\ttrain_loss\taction\tprediction\tverified\tdeferred\tutility"
                   "\tmodel_before\tmodel_after\n",
                   run->baseline_training.mean_loss, run->baseline_training.correct,
                   run->baseline_training.pack_hash, run->baseline_development.mean_loss,
                   run->baseline_development.correct, run->baseline_confirmation.mean_loss,
                   run->baseline_confirmation.correct, run->training.mean_loss,
                   run->training.correct, run->training.pack_hash, run->development.mean_loss,
                   run->development.correct, run->confirmation.mean_loss, run->confirmation.correct,
                   run->prepared_generation, run->initial_choices.version) >= 0 &&
           fflush(run->report) == 0;
}

static int write_choice(FILE *file, const evolve_trial *trial) {
    const cgai_life_context_choice *choice = &trial->choice;
    if (fprintf(file,
                "choice_token\t%" PRIu64 "\nchoice_input_hash\t%" PRIu64
                "\ninput_bytes_hash\t%" PRIu64 "\nmodel_version_before\t%" PRIu64
                "\naction\t%u\nfallback\t%u\nprediction_before\t%.17g"
                "\nsupported_groups\t%u\nallowed_actions\t%zu"
                "\ndomain_frontier_bits\t%u\ncode_edits\t%u\n",
                choice->token, choice->input_hash,
                evolve_bytes_hash(trial->input, strlen(trial->input)), choice->model_version,
                choice->action, trial->fallback, choice->predicted_utility,
                choice->supported_groups, trial->action_count,
                trial->candidate.domain_frontier_bits, trial->candidate.edit_count) < 0)
        return 0;
    for (size_t i = 0U; i < trial->action_count; ++i)
        if (fprintf(file, "allowed_action\t%u\n", trial->actions[i]) < 0)
            return 0;
    return 1;
}

static int write_choice_slices(FILE *file, const evolve_trial *trial, int after) {
    const cgai_life_context_choice_stats *stats = after ? &trial->after : &trial->before;
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        if (fprintf(file, "choice_group_%s\t%zu\t%" PRIu64 "\t%" PRIu64 "\n",
                    after ? "after" : "before", group, stats->group_steps[group],
                    stats->group_hashes[group]) < 0)
            return 0;
    return 1;
}

int evolve_report_choice(const evolve_work *work, const evolve_trial *trial) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    if (!evolve_path_join(path, sizeof(path), work->directory, "choice.tsv"))
        return 0;
    FILE *file = fopen(path, "wbx");
    if (file == NULL)
        return 0;
    int okay = fputs("LIFE_SOURCE_CHOICE\t1\n", file) >= 0 && write_choice(file, trial) &&
               write_event(file, trial->event) && write_choice_slices(file, trial, 0) &&
               fputs("END\n", file) >= 0 && fflush(file) == 0;
    if (fclose(file) != 0)
        okay = 0;
    return okay;
}

static int write_event_state(FILE *file, const cgai_life_collision_event *event) {
    return fprintf(file,
                   "conflict_age\t%u\nfrontier_count\t%u\nlegal_output_mask\t%u\noutcome\t%u"
                   "\nteacher_target_valid\t%u\nteacher_target\t%u\nsource_policy_version\t%" PRIu64
                   "\nresult_policy_version\t%" PRIu64 "\n",
                   event->conflict_age, event->frontier_count, event->legal_output_mask,
                   (unsigned int)event->outcome, event->teacher_target_valid, event->teacher_target,
                   event->source_policy_version, event->result_policy_version) >= 0;
}

static int write_event(FILE *file, const cgai_life_collision_event *event) {
    if (!write_event_state(file, event) ||
        fprintf(file,
                "generation\t%u\nconflict\t%u\nparticipant_mask\t%u\nselected_output\t%u"
                "\ntoggle_bits\t%u\nsource_world_checksum\t%" PRIu64
                "\nresult_world_checksum\t%" PRIu64 "\n",
                event->generation, event->conflict_id, event->participant_mask,
                event->selected_output, event->toggle_bits, event->source_world_hash,
                event->result_world_hash) < 0)
        return 0;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        if (fprintf(file, "parent\t%zu\t%u\t%u\n", i, event->group_uids[i],
                    event->group_ancestry[i]) < 0)
            return 0;
    for (size_t i = 0U; i < event->frontier_count; ++i)
        if (fprintf(file, "frontier\t%zu\t%u\n", i, event->frontier_cells[i]) < 0)
            return 0;
    return 1;
}

static int write_edits(FILE *file, const evolve_mutation_candidate *candidate) {
    if (fprintf(file,
                "LIFE_SOURCE_MUTATION\t2\nsource_checksum\t%" PRIu64
                "\ncandidate_checksum\t%" PRIu64 "\nedits\t%u\n"
                "domain_frontier_bits\t%u\n"
                "edit_columns\tsite\tsource_offset\tcandidate_offset\tbefore\tafter\n",
                candidate->source_checksum, candidate->checksum, candidate->edit_count,
                candidate->domain_frontier_bits) < 0)
        return 0;
    for (size_t i = 0U; i < candidate->edit_count; ++i) {
        const evolve_mutation_edit *edit = &candidate->edits[i];
        if (fprintf(file, "edit\t%u\t%zu\t%zu\t%s\t%s\n", edit->site, edit->source_offset,
                    edit->candidate_offset, edit->before_literal, edit->after_literal) < 0)
            return 0;
    }
    return 1;
}

static int write_feedback(FILE *file, const evolve_trial *trial) {
    return fprintf(file,
                   "feedback_split\ttrain\nfeedback_verified\t%u\nfeedback_deferred\t%u"
                   "\nevidence_hash\t%" PRIu64 "\nutility\t%.17g"
                   "\nparent_training_loss\t%.17g\ncandidate_training_loss\t%.17g"
                   "\nmodel_version_after\t%" PRIu64 "\naction_prediction_after\t%u"
                   "\nprediction_after\t%.17g\n",
                   trial->feedback.verified, trial->feedback.deferred,
                   trial->feedback.evidence_hash, trial->feedback.utility,
                   trial->parent_training_loss, trial->training.mean_loss, trial->after.version,
                   trial->prediction_after.action,
                   trial->prediction_after.predicted_utility) >= 0 &&
           write_choice_slices(file, trial, 0) && write_choice_slices(file, trial, 1);
}

static int candidate_provenance(const evolve_work *work, const evolve_trial *trial) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    if (!evolve_path_join(path, sizeof(path), work->directory, "changes.tsv"))
        return 0;
    FILE *file = fopen(path, "wbx");
    if (file == NULL)
        return 0;
    int okay = write_edits(file, &trial->candidate) && write_event(file, trial->event) &&
               write_choice(file, trial) && write_feedback(file, trial) &&
               fputs("END\n", file) >= 0 && fflush(file) == 0;
    if (fclose(file) != 0)
        okay = 0;
    return okay;
}

int evolve_report_candidate(evolve_run *run, const evolve_work *work, const evolve_trial *trial,
                            const char *status) {
    const evolve_mutation_candidate *candidate = &trial->candidate;
    const cgai_life_collision_event *event = trial->event;
    if (!candidate_provenance(work, trial))
        return 0;
    return fprintf(run->report,
                   "candidate\t%u\t%s\t%" PRIu64 "\t%" PRIu64 "\t%u\t%u\t%u\t%u\t%s\t%.17g"
                   "\t%.17g\t%.17g\t%u\t%.17g\t%u\t%u\t%.17g\t%" PRIu64 "\t%" PRIu64 "\n",
                   run->proposals, work->source, candidate->source_checksum, candidate->checksum,
                   event->generation, event->conflict_id, event->selected_output,
                   candidate->edit_count, status, trial->development.mean_loss,
                   trial->confirmation.mean_loss, trial->training.mean_loss, trial->choice.action,
                   trial->choice.predicted_utility, trial->feedback.verified,
                   trial->feedback.deferred, trial->feedback.utility, trial->before.version,
                   trial->after.version) >= 0 &&
           fflush(run->report) == 0;
}

int evolve_report_finish(evolve_run *run, uint32_t generations) {
    cgai_life_context_stats context;
    cgai_life_context_choice_stats choices;
    if (cgai_life_context_get_stats(run->memory, &context) != CGAI_LIFE_OK ||
        cgai_life_context_choice_get_stats(run->memory, &choices) != CGAI_LIFE_OK)
        return 0;
    return fprintf(run->report,
                   "summary\tproposals\t%u\taccepted\t%u\tgenerations\t%u\n"
                   "search_status\t%s\ninvocation_generations\t%u\nfinalized\t%u"
                   "\nactivity_generations_applied\t%u"
                   "\ncompleted_receipts\t%u\ncheckpoint_hash\t%" PRIu64 "\ncheckpoint_bundle\t%s\n"
                   "best_source_checksum\t%" PRIu64 "\nmemory_checksum\t%" PRIu64
                   "\ncontext_generation\t%u\ncontext_records\t%u\ncontext_fitted\t%u"
                   "\ncontext_deferred\t%u\ncontext_updates\t%" PRIu64
                   "\nchoice_decisions\t%" PRIu64 "\nchoice_observations\t%" PRIu64
                   "\nchoice_deferred\t%" PRIu64 "\nchoice_version\t%" PRIu64 "\nEND\n",
                   run->proposals, run->accepted, generations,
                   run->paused != 0U ? "paused" : "complete", run->invocation_generations,
                   run->finalized, run->finalized != 0U ? EVOLVE_CONTEXT_ACTIVITY_GENERATIONS : 0U,
                   run->ledger_count, run->checkpoint_hash, run->latest_checkpoint,
                   run->parent.checksum, cgai_life_context_hash(run->memory), context.generation,
                   context.records, context.trained_records, context.deferred_records,
                   context.domain_updates, choices.decisions, choices.observations,
                   choices.deferred, choices.version) >= 0 &&
           fflush(run->report) == 0;
}
