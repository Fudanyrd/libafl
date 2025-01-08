//! The setcover scheduler.

use std::borrow::ToOwned;

use libafl_bolts::ErrorBacktrace;
use crate::{
    corpus::{Corpus, CorpusId}, 
    schedulers::{RemovableScheduler, Scheduler}, 
    state::HasCorpus, 
    Error
};

use super::HasQueueCycles;

/// The setcover scheduler.
#[derive(Debug, Clone)]
pub struct SetcoverScheduler {
    // The number of cycles fuzzed.
    num_cycles: u64,
}

impl<I, S> RemovableScheduler<I, S> for SetcoverScheduler {}

impl<I, S> Scheduler<I, S> for SetcoverScheduler 
where
    S: HasCorpus
{
    fn on_add(&mut self, _state: &mut S, _id: CorpusId) -> Result<(), Error> {
        Err(Error::NotImplemented(
            "SetcoverScheduler::on_add is not implemented"
                .to_owned(),
            ErrorBacktrace::new(),
        ))
    }

    fn next(&mut self, state: &mut S) -> Result<CorpusId, Error> {
        if state.corpus().count() == 0 {
            return Err(Error::empty(
                "No entries in corpus. This often implies the target is not properly instrumented."
                    .to_owned(),
            ));
        } else {
            self.num_cycles += 1;
            let id = state
                .corpus()
                .current()
                .map(|id| state.corpus().next(id))
                .flatten()
                .unwrap_or_else(|| state.corpus().first().unwrap());

            <Self as Scheduler<I, S>>::set_current_scheduled(self, state, Some(id))?;
            return Ok(id);
        }
    }

    fn set_current_scheduled(&mut self, state: &mut S, 
        next_id: Option<CorpusId>,
    ) -> Result<(), Error> {
        if next_id == None {
            return Err(Error::Empty(
                "No next id provided."
                    .to_owned(),
                ErrorBacktrace::new()
            ));
        } else {
            *state
                .corpus_mut()
                .current_mut() = next_id;
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
