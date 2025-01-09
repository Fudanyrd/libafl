//! The setcover scheduler.

use core::ops::DerefMut;
use std::borrow::ToOwned;

use crate::{
    corpus::{Corpus, CorpusId},
    schedulers::{RemovableScheduler, Scheduler},
    state::{HasCorpus, HasSetCover},
    Error,
};
use libafl_bolts::ErrorBacktrace;

use super::HasQueueCycles;

/// The setcover scheduler.
#[derive(Debug, Clone)]
pub struct SetcoverScheduler {
    // The number of cycles fuzzed.
    num_cycles: u64,
}

impl<I, S> RemovableScheduler<I, S> for SetcoverScheduler {}

impl SetcoverScheduler {
    /// Creates a new [`SetcoverScheduler`].
    pub fn new() -> Self {
        Self::default()
    }
}

impl<I, S> Scheduler<I, S> for SetcoverScheduler
where
    S: HasCorpus + HasSetCover,
{
    fn on_add(&mut self, state: &mut S, id: CorpusId) -> Result<(), Error> {
        // Set parent id
        if true {
            let current_id: Option<CorpusId> = *state.corpus().current();

            let mut input_ref: core::cell::RefMut<
                '_,
                crate::corpus::Testcase<<<S as HasCorpus>::Corpus as Corpus>::Input>,
            > = state
                .corpus()
                .get(id)
                .unwrap()
                .borrow_mut();

            let input = input_ref
                .deref_mut();

            match input.use_setcover_schedule() {
                Ok(()) => {}
                Err(_) => {
                    return Err(Error::empty(
                        "Input already has a setcover schedule.".to_owned(),
                    ));
                }
            }
            input.set_parent_id_optional(current_id);
        }

        // when we bump into a new path, we call update_bitmap_score()
        // to see if the path appears more favorable than existing ones.
        if true {
            state.update_bitmap_score(id);
        }
        return Ok(());
    }

    fn next(&mut self, state: &mut S) -> Result<CorpusId, Error> {
        if state.corpus().count() == 0 {
            return Err(Error::empty(
                "No entries in corpus. This often implies the target is not properly instrumented."
                    .to_owned(),
            ));
        } else {
            self.num_cycles += 1;

            // select next seed.
            state.cull_queue();

            // try to get the favored id.
            let mut id = state.corpus().get_favored_id();

            if id == None {
                // use next id.
                let default_id = state
                    .corpus()
                    .current()
                    .map(|id| state.corpus().next(id))
                    .flatten()
                    .unwrap_or_else(|| state.corpus().first().unwrap());

                id = Some(default_id);
            }

            <Self as Scheduler<I, S>>::set_current_scheduled(self, state, id)?;
            return Ok(id.unwrap());
        }
    }

    fn set_current_scheduled(
        &mut self,
        state: &mut S,
        next_id: Option<CorpusId>,
    ) -> Result<(), Error> {
        if next_id == None {
            return Err(Error::Empty(
                "No next id provided.".to_owned(),
                ErrorBacktrace::new(),
            ));
        } else {
            *state.corpus_mut().current_mut() = next_id;
            return Ok(());
        }
    }
}

impl HasQueueCycles for SetcoverScheduler {
    fn queue_cycles(&self) -> u64 {
        return self.num_cycles;
    }
}

impl Default for SetcoverScheduler {
    fn default() -> Self {
        Self { num_cycles: 0 }
    }
}
