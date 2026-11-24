"""Estado de membros preservado pelo yt-dlp ou pelo catálogo da conta."""
LABELS = {"members only", "members-only", "apenas para membros", "só para membros", "somente para membros"}


def _badge(value, inside=False):
    if isinstance(value, list):
        return any(_badge(v, inside) for v in value)
    if not isinstance(value, dict):
        return False
    for key, v in value.items():
        if key == "isMembersOnly" and v is True:
            return True
        if inside and isinstance(v, str):
            if key in ("style", "badgeStyle") and v in ("BADGE_STYLE_TYPE_MEMBERS_ONLY", "BADGE_MEMBERS_ONLY"):
                return True
            if key in ("iconType", "iconName") and v == "SPONSORSHIP_STAR":
                return True
            if key in ("label", "text", "badgeText", "simpleText", "content") and v.lower() in LABELS:
                return True
        if key in ("menu", "ownerBadges", "navigationEndpoint", "onSelectCommand", "onLongPressCommand"):
            continue
        if _badge(v, inside or key in ("badges", "badge", "badgeViewModel") or "BadgeRenderer" in key):
            return True
    return False


def membership(entry):
    state = entry.get("membership")
    if state in ("members", "member"):
        return state
    if entry.get("availability") == "subscriber_only" or _badge(entry):
        return "member" if entry.get("isPlayable") is True else "members"
    return ""
