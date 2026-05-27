# display
See [display/taste.md](display/taste.md)
# workflow
See [workflow/taste.md](workflow/taste.md)
# uncategorized
- Use `const` for function params that are not modified. Confidence: 0.80
- Format C++ code with K&R indentation style. Confidence: 0.75
- For C++ header files: use `#pragma once` include guard. Confidence: 0.75
- Keep column width within 100 chars for readability. Confidence: 0.70

# display (second occurrence - merged)
See [display-(second-occurrence---merged)/taste.md](display-(second-occurrence---merged)/taste.md)
# feedback
- Make ONE fix at a time, commit, build, flash, then STOP and wait for user feedback before making any further changes. Do not revert to previous states without explicit instruction. Confidence: 0.95
- Never revert a change the user explicitly requested, even if you think it might cause issues. The user knows their hardware. Confidence: 0.95
- When a user explicitly says "don't touch [component]", stop making any edits to that component and drop that line of investigation completely. Confidence: 0.90

# hardware
- For this BBA platform: the RGB LED strip on GPIO23 may not be physically present; make its initialization optional/conditional rather than assuming it exists. Confidence: 0.85

# workflow
- Do not rebuild code that has already been flashed to the device; ask user if they need a rebuild first. Confidence: 0.90

# git
- Use `git co <hash> -- <file>` to revert a single file to a specific commit state. Confidence: 0.80
- Use `git diff --cached` to review staged changes before committing. Confidence: 0.75
- Use `git rebase -i` to squash commits on feature branch before merging. Confidence: 0.70

# debugging
- When both encoder AND buttons stop responding simultaneously, the root cause is in shared input infrastructure (input task crash, queue issue, UI task loop), not unrelated hardware pin changes. Confidence: 0.70
- When a user insists a specific code change (e.g., calling `GetCStrJSONConfiguration()` during boot) is the freeze trigger while another similar function works fine (e.g., `GetCStrJSONSoundProcessors()`), compare the exact implementation difference — one reloads the model/document from flash before reading (`loadJSON`), the other doesn't — this points to a stale/inconsistent model state causing the freeze. Confidence: 0.70
- When debugging a freeze/crash: prioritize root cause analysis over rebuilding and re-flashing the same code. Do not re-build and re-flash without first identifying the root cause. Confidence: 0.78
- When asked to investigate a specific commit, read the commit diff directly (`git diff <commit>^..<commit>`) rather than exploring files that may have changed. Confidence: 0.65
- When the user points at a specific component (e.g., "check its API requests, memory") as the likely cause of a bug, follow that lead aggressively and trace the resource/API path through that component before exploring unrelated changes. The user's diagnostic hints should be trusted as the primary investigative direction. Confidence: 0.70
