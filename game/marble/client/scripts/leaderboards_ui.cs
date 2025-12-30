// Example in-game UI helper: parse leaderboard text and create simple console output.
// This is a minimal example. Integrate into your UI system as needed.
//
// Expected input: newline separated lines `name|time|level` OR JSON array if using format=json.

function LB_UI_PrintLeaderboardText(%text)
{
  if (%text $= "") { echo("Leaderboard: (empty)"); return; }

  // Detect JSON
  if (getSubStr(%text, 0, 1) $= "[" || getSubStr(%text, 0, 1) $= "{")
  {
    // naive JSON handling: TorqueScript doesn't have robust JSON parsing by default.
    // If your engine build has JSON support, use it. Otherwise, just print raw text.
    echo("Leaderboard (JSON): " @ %text);
    return;
  }

  // Plain text parsing
  %lines = %text;
  %count = getFieldCount(%lines, "\n");
  if (%count == 0) {
    // fallback: getWordCount with newline delimiter
    %count = getWordCount(%lines, "\n");
  }
  echo("Leaderboard (top " @ %count @ "):");
  for (%i = 0; %i < getWordCount(%lines, "\n"); %i++)
  {
    %line = getWord(%lines, %i, "\n");
    if (%line $= "") continue;
    // split by | char
    %parts = LB_UI_SplitLine(%line, "|");
    %name = getField(%parts, 0);
    %time = getField(%parts, 1);
    %level = getField(%parts, 2);
    echo("  " @ (%i+1) @ ") " @ %name @ " - " @ %time @ "s (level: " @ %level @ ")");
  }
}

function LB_UI_SplitLine(%line, %sep)
{
  // return a TAB-separated field list using strReplace trick
  %tmp = strReplace(%line, %sep, "\t");
  return %tmp;
}

// Example usage:
// LB_RequestLeaderboard("level1", 10, Player, "onLeaderboardReceived");
// function Player.onLeaderboardReceived(%this, %success, %entriesTxt)
// {
//   if (!%success) { echo("Failed to fetch leaderboard"); return; }
//   LB_UI_PrintLeaderboardText(%entriesTxt);
// }