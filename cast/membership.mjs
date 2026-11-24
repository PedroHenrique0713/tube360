const labels = new Set(['members only', 'members-only', 'apenas para membros', 'só para membros', 'somente para membros']);
function badge(value, inside = false) {
  if (Array.isArray(value)) return value.some(v => badge(v, inside));
  if (!value || typeof value !== 'object') return false;
  return Object.entries(value).some(([key, v]) => {
    if (key === 'isMembersOnly' && v === true) return true;
    if (inside && typeof v === 'string') {
      if (['style', 'badgeStyle'].includes(key) && ['BADGE_STYLE_TYPE_MEMBERS_ONLY', 'BADGE_MEMBERS_ONLY'].includes(v)) return true;
      if (['iconType', 'iconName'].includes(key) && v === 'SPONSORSHIP_STAR') return true;
      if (['label', 'text', 'badgeText', 'simpleText', 'content'].includes(key) && labels.has(v.toLowerCase())) return true;
    }
    if (['menu', 'ownerBadges', 'navigationEndpoint', 'onSelectCommand', 'onLongPressCommand'].includes(key)) return false;
    return badge(v, inside || ['badges', 'badge', 'badgeViewModel'].includes(key) || key.includes('BadgeRenderer'));
  });
}
export function membership(video) {
  return badge(video) ? video.isPlayable === true ? 'member' : 'members' : '';
}
