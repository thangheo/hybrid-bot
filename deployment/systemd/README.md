Installation (system-wide):

1. Copy unit files to /etc/systemd/system (requires sudo):

   Note: the service file provided is a template; you must edit the WorkingDirectory and ExecStart to the absolute path
   where you checked out this repository. Example (replace /path/to/repo):

   sudo cp deployment/systemd/hybrid-trainer.service /etc/systemd/system/
   sudo cp deployment/systemd/hybrid-trainer.timer /etc/systemd/system/
   sudo editor /etc/systemd/system/hybrid-trainer.service
   # set WorkingDirectory=/path/to/repo and ExecStart=/path/to/repo/scripts/trainer_wrapper.sh

2. Make the wrapper executable and reload systemd:

   sudo chmod +x scripts/trainer_wrapper.sh
   sudo systemctl daemon-reload

3. Enable and start the timer (this will activate the service on schedule):

   sudo systemctl enable --now hybrid-trainer.timer

4. Check timer and last run:

   sudo systemctl list-timers --all | grep hybrid-trainer
   sudo journalctl -u hybrid-trainer.service -b


Installation (per-user, no sudo):

1. Copy units to the user systemd directory and edit ExecStart if needed:

   mkdir -p ~/.config/systemd/user
   cp deployment/systemd/hybrid-trainer.service ~/.config/systemd/user/
   cp deployment/systemd/hybrid-trainer.timer ~/.config/systemd/user/
   # Edit the .service file to set ExecStart to the absolute path of scripts/trainer_wrapper.sh

2. Make wrapper executable:

   chmod +x scripts/trainer_wrapper.sh

3. Reload user systemd and enable timer:

   systemctl --user daemon-reload
   systemctl --user enable --now hybrid-trainer.timer

4. Inspect logs:

   journalctl --user -u hybrid-trainer.service

Notes:
- The wrapper will call "./trading_manager.sh stop" (using the repo-relative path) before training and "./trading_manager.sh start" after training.
- The training command currently used in the wrapper is:

  python3 <repo-root>/python-strategy/regime.py --start-trainer --n-states 4 --trainer-interval-days 30 --data-source binance

  If you want a different trainer interval (e.g., 14 days), edit the wrapper or the timer accordingly.

- If you prefer cron or a different scheduler, you can instead add a cron entry that runs the wrapper every 14 days.
