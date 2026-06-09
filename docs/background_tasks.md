# Background tasks

PulsePadGTK uses `pulsepad::TaskRunner` for work that might block the GTK main loop.
Use it for file I/O, subprocesses, waveform generation, package import/export, and other operations that can take noticeable time.

## Submitting work

Call `taskRunner.submit(jobName, work, completion)` from the UI layer. The `work` callback runs on a worker thread and receives a `CancellationToken`. The `completion` callback receives a `TaskOutcome<T>` and is delivered by the runner's completion executor. In the GTK app, that executor posts through `Glib::signal_idle().connect_once()` so completions run on the GTK main loop.

Use stable job names such as `audio-import`, `waveform-generate`, `board-import`, `board-export`, and `reverse-audio`; these names appear in diagnostics.

## GTK thread rules

Never touch GTK widgets from `work`. Read any needed values before submitting the task, copy them into the worker lambda, and update widgets only from `completion`. Completion callbacks must still check lifetime guards such as `windowAlive`, dialog-alive flags, and generation ids before mutating UI.

## Cancellation

`submit()` returns a `TaskHandle`. Store the handle when a user action might invalidate the task. Call `cancel()` when a dialog closes, a newer waveform generation supersedes an older one, or shutdown begins. Worker callbacks should check `token.cancellation_requested()` around expensive steps. Subprocesses must keep their existing timeouts because cancellation is cooperative.

## Shutdown

Window destruction marks UI targets dead, cancels reverse jobs, and asks the task runner to stop. Completion callbacks must return immediately when their target window/dialog is gone. This prevents late results from touching destroyed widgets and avoids raw detached threads during shutdown.
