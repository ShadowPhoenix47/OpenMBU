// Client-side leaderboard glue: receives server instruction to submit score and handles leaderboard display.

// Ensure HTTP object exists (the HTTPObject helper is present in many Torque builds)
if (!isObject(LB_HTTP))
{
  new HTTPObject(LB_HTTP);
}

// internal mapping from request id to callback (already used by LB_RequestLeaderboard / LB_SubmitScore helpers)
$LB::RequestCounter = $LB::RequestCounter + 0; // keep if already defined

// Client RPC: server calls commandToClient(%cl, 'SubmitScore', name, timeSec, levelId)
// On the client this becomes clientCmdSubmitScore(...)
function clientCmdSubmitScore(%name, %time, %level)
{
  // Defensive: ensure LB_SubmitScore is available (this function is provided by the leaderboards integration)
  if (!isFunction("LB_SubmitScore"))
  {
    // If absent, print to console and exit.
    echo("[Leaderboards] LB_SubmitScore not available; can't submit score. Name:" SPC %name SPC "time:" SPC %time SPC "level:" SPC %level);
    return;
  }

  // Submit the score (this will perform an HTTP POST to /score on your configured leaderboard server).
  echo("[Leaderboards] Submitting score: " @ %name @ " time:" @ %time @ " level:" @ %level);
  LB_SubmitScore(%name, %time, %level);

  // After a short delay, refresh the local leaderboard display so the UI shows the updated ranking.
  // A small delay helps ensure the remote server has time to accept the score.
  schedule(1500, 0, "LB_RequestLeaderboard", %level, 10, GameEndGui, "onLeaderboardReceived", "text");
}

// Callback invoked by LB_RequestLeaderboard when leaderboard data arrives.
// Signature used by LB_RequestLeaderboard: YourObject.onLeaderboardReceived(success, entriesTxt)
function GameEndGui::onLeaderboardReceived(%this, %success, %entriesTxt)
{
  if (!isObject(GE_Stats))
     return;

  GE_Stats.clear();

  if (!%success)
  {
     GE_Stats.addRow(0, "Failed to load leaderboard");
     return;
  }

  // entriesTxt is newline-separated lines of 'name|time|level'
  %linesCount = getWordCount(%entriesTxt, "\n");
  if (%linesCount == 0)
  {
     GE_Stats.addRow(0, "No leaderboard entries");
     return;
  }

  %rowId = 0;
  for (%i = 0; %i < %linesCount; %i++)
  {
     %line = getWord(%entriesTxt, %i, "\n");
     if (%line $= "") continue;

     // Replace '|' with tab to use getField later if needed; GuiTextListCtrl expects fields separated by TAB for columns.
     %rowText = strReplace(%line, "|", "\t");

     // Add each leaderboard line as a row. Use a numeric id (string) and display text containing fields.
     GE_Stats.addRow(%rowId++, %rowText);
  }
}