# Channel pagination fix — the `/streams` tab poisoned the page count

## Symptom

Opening a channel showed **no page numbers at the bottom** — the pager bar
was gone entirely, even for channels with hundreds of videos. Paging still
worked internally, but the user had no way to navigate pages.

The bug was specific to **page 1** of a channel. Page 2 onward would have
shown a pager — but with no controls on page 1, you could never get there.

## Root cause

Channel page 1 deliberately fetches **two** tabs at once and merges them, so a
currently-live or upcoming stream can be floated to the top of the list:

```
/@channel/videos     ← the uploads
/@channel/streams    ← live / upcoming / past streams
```

`Extractor::handleFinished()` derives the playlist *total* from yt-dlp's
`playlist_count` field, taking the maximum seen across every NDJSON line:

```cpp
const int pc = obj.value("playlist_count").toInt(0);
if (pc > total) total = pc;
...
emit searchTotalKnown(total);   // -> SearchPage::totalItems_
```

The intent was that a real total would yield real page numbers. But:

| Tab        | `playlist_count` reported by yt-dlp |
|------------|-------------------------------------|
| `/videos`  | `None` → 0 (channels don't expose an exact upload count) |
| `/streams` | the **stream count**, e.g. `11`     |

So on page 1 the merged `total` became the **stream count** (11), not the
channel's video count. The UI then computed:

```
lastPage = ceil(totalItems_ / pageSize) = ceil(11 / 20) = 1
pager.setVisible(canPaginate && lastPage > 1)  // 1 > 1 == false  -> HIDDEN
```

One page, no pager. On page 2+ the `/streams` tab isn't fetched, so `total`
fell back to 0 and the heuristic pager would have worked — but page 1 gave no
way to get there.

This surfaced as a regression because YouTube/yt-dlp began reporting a
`playlist_count` on the `/streams` tab; previously it was absent, so `total`
stayed 0 and the channel correctly fell back to the page-size heuristic.

### Why playlist views are unaffected

Real playlists (`list=` / `/playlist`) no longer go through this code path at
all — they were moved to the InnerTube browse API
(`fetchPlaylistPage` → `handlePlaylistPageReply`), which derives its total from
`findPlaylistTotal()`. The `runFlat`/`handleFinished` path now only serves
search, channel `/videos`, channel `/streams`, and in-channel search — none of
which have a meaningful `playlist_count`.

## The fix

Ignore `playlist_count` from any `/streams` entry, so the streams tab's count
can never become the channel's total. The `/videos` tab reports no count, so
channels cleanly fall back to the `rawCount >= pageSize` heuristic in the UI —
the intended behaviour for open-ended tabs (the pager shows `1 2 …` and grows
as you page).

The `/streams` source check already existed for dropping finished past
streams; it's now hoisted once and reused for both purposes.

```cpp
const QString src  = obj.value("playlist_webpage_url").toString();
const QString orig = obj.value("original_url").toString();
const bool fromStreams = src.endsWith("/streams") || orig.endsWith("/streams");

if (!fromStreams) {                       // streams count never sets the total
    const int pc = obj.value("playlist_count").toInt(0);
    if (pc > total) total = pc;
}
...
if (pinLiveFirst_ && r.durationSeconds > 0 && fromStreams) {
    continue;                             // drop finished past streams
}
```

## Verification

Confirmed against a live channel (`@TED`) with yt-dlp:

- `/videos` tab → `playlist_count = None`
- `/streams` tab → `playlist_count = 11`
- Page-1 merge previously yielded `total = 11` → `lastPage = 1` → pager hidden.

After the fix, `total = 0` for the channel page, so the UI uses the rawCount
heuristic (`20 >= 20` → there is a next page) and the pager is shown again.

Builds clean (`cmake --build build`).
