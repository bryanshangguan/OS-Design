OS-Design # create README content

# REPORT 


# PART 3

# Explanation of the Bug

The program gets stuck in an infinite loop because of how setcontext and uc_link work together. When main calls setcontext(&worker1_ctx), it jumps into the worker. After the worker finishes, it comes back to main through uc_link, but it resumes right where setcontext was called. That makes main set up the worker again and call setcontext again, so the same process keeps repeating and the program never ends.

# Change to Ensure Termination

To stop the loop, we add a guard variable (resumed_to_main). The first time main runs, we set this flag. Then, when the worker finishes and control comes back to main through uc_link, the flag tells us we’ve already been here once. Instead of setting up the worker again, main just frees the stack and ends the program. This way, the worker only runs once, comes back to main once, and the program finishes normally.

- Bug Illustration:

Main
  ↓ (init_context prepares worker1_ctx)
Main
  ↓ (setcontext -> jump to worker)
Worker1 runs
  ↓ (finishes, jumps back via uc_link to main_ctx)
Main resumes
  ↓ (but no guard, so it re-runs the same logic)
Main
  ↓ (init_context prepares worker1_ctx again)
Main
  ↓ (setcontext -> jump to worker again)
Worker1 runs
  ↓ (finishes, jumps back via uc_link)
... repeats forever ...

- Fixed Code:

Main
  ↓ (init_context prepares worker1_ctx)
Main
  ↓ (setcontext -> jump to worker)
Worker1 runs
  ↓ (finishes, jumps via uc_link)
Main resumes
  ↓ (guard detects return)
Main cleans up and exits ✅

