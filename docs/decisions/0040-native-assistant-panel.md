# ADR 0040: native assistant panel, setup and publication controls

Implemented; acceptance is recorded separately. This decision does
not claim live OpenAI quality or successful local CPU modeling.

The panel owns one provider task over the native bridge. Provider construction,
credential lookup and journal binding are lazy; opening an unconfigured editor
does not access a keyring or network. The window destroys the panel before its
native document. Provider destruction precedes bridge destruction. Replacing or
closing a model retires its task while the original document session is still
valid; an unresolved publication blocks replacement.

OpenAI is the chosen remote provider, with an explicit user-configured model ID.
The nonmodal Preferences dialog stores keys through `OpenAiCredentialStore` and
clears the password field after handing off its bytes. No key is saved in settings,
models or transcripts; there is no plaintext fallback. Lookup happens when a user
sends a request. Tests inject a disposable helper and never access the user's OS
credential facility. Local settings expose the measured experimental Ollama
profile, including its failed CPU corpus, and never provision a service or model.

The first remote request needs an actual per-provider consent-button click.
Disclosure includes the prompt, initial attachments and broader bounded document
inspection available through tools. It excludes screenshots and image assets.
Selection attachments are up to seven exact entity descriptions; the optional
editing-context attachment describes its actual context (or the model root).
Removing attachments affects the initial context, not the explicitly disclosed
tool-access scope. The document snapshot is pinned while consent/key lookup is
pending; a change requires a new request.

The native panel presents the current original prompt, audited tool activity,
plain unverified provider explanation, raw transcript disclosure, structured
clarification and host Apply/Discard/Refine controls. Clarification uses the
bounded task protocol. A viewport-selection answer serializes actual typed entity
references; it does not use provider-invented coordinates or grant new commands.
The same revision/deadline checks run before reacquiring a preview, so a manual
edit retains the engine's Stale state instead of being mislabeled as Discard.

Preview-first is the default. Direct mode still acquires and displays the sealed,
validated proposal and calls the same host Apply path. Native gestures and modal
dialogs delay publication. Deletion and shared-definition commands require explicit
per-request controls; they are absent from the default allowlist. The routine
allowlist supports exact room/window recipes and ordinary bounded modeling.
Checkbox authorization resets for the next request. Semantic correctness is not
inferred from a successful tool call or a render image.

Proposal inspection uses the exact immutable snapshot. Counts distinguish body
records; unchanged claims compare those records and world transforms. Material,
tag, definition and asset changes are called out separately. Per-entity bounds,
area and available solid volume are measured by the host; labels do not pretend
world bounds are semantic clear-opening measurements. Up to 200 changed bodies
are listed, with the limit stated. Live viewport picking remains distinct from
proposal-list inspection. The viewport banner clearly marks unapplied geometry.

Unknown outcomes disable modeling controls, save/replacement and automatic
recovery checkpoints while leaving Reconcile available. Public modeling actions
also check the fence. Reconciliation uses the original request identity, publishes
once, refreshes native selection/history and restores editing. A committed result
is shown only from the transaction receipt; unknown is never called unchanged.
The Undo button applies only while this task is still the top applied history
entry, so it cannot silently undo subsequent human work.

The assistant shares the model-side tabs at standard widths, has a separate wide
column, and uses a nonmodal sheet on narrower tiles. Only the assistant widget is
reparented; the live OpenGL viewport stays in place. Ctrl+J toggles/focuses the
composer, Ctrl+Return applies a staged proposal, and Escape returns to the model.

During an active task the disabled request composer is hidden, leaving room for
the question or measured proposal. Apply/Discard stay outside the scrolling
history; validated steps and raw activity are available through disclosures.
