/* chat_filter_test — the rules the Rust port must also pass (see
 * recomp-net-server/src/chat_filter.rs tests; keep the two in step). */
#include "recomp_net/chat_filter.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

static void expect(const char *in, const char *want)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", in);
    rnet_chat_filter_apply(buf, sizeof(buf));
    if (strcmp(buf, want) != 0) {
        printf("FAIL: %-32s -> %-32s (want %s)\n", in, buf, want);
        g_fail = 1;
    }
}

int main(void)
{
    if (!rnet_chat_filter_enabled()) { puts("filter disabled?"); return 1; }
    printf("%d entries\n", rnet_chat_filter_word_count());

    /* plain, case, repeats, leet, spaced out, fullwidth */
    expect("fuck", "****");
    expect("FUCK you", "**** you");
    expect("fuuuuck", "*******");
    expect("sh1t happens", "**** happens");
    expect("$hit", "****");
    expect("f.u.c.k", "*******");
    expect("f u c k off", "******* off");
    expect("ｆｕｃｋ", "****");
    expect("what the fuck?!", "what the ****?!");
    expect("asshole!", "*******!");

    /* whole-word entries stay inside their word boundaries */
    expect("class assassin", "class assassin");
    expect("Scunthorpe", "Scunthorpe");
    expect("shitake mushrooms", "shitake mushrooms");
    expect("cumulative", "cumulative");
    expect("bass fishing", "bass fishing");
    expect("compute puta", "compute ****");
    expect("hello", "hello");
    expect("a s s", "a s s");            /* 3 letters: no separator bridging */

    /* substring entries and non-Latin scripts */
    expect("n1gger", "******");
    expect("Sniggers", "S******s");       /* '~' entry: inside a word too */
    expect("блять", "*****");
    expect("СУКА блин", "**** блин");
    expect("blyat", "*****");
    expect("死ね", "**");
    expect("お前死ね!", "お前**!");
    expect("傻逼", "**");
    expect("씨발 진짜", "** 진짜");
    expect("scheiße", "*******");
    expect("putain de merde", "****** de *****");
    expect("filho da puta", "*************");
    expect("ヽ(´ー｀)ノ", "ヽ(´ー｀)ノ");

    /* emoji and length are preserved elsewhere */
    expect("gg 😀 fuck 😀", "gg 😀 **** 😀");

    if (g_fail) { puts("chat_filter_test: FAILED"); return 1; }
    puts("chat_filter_test: passed");
    return 0;
}
