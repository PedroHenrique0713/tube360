"""Reduz gravações públicas locais e remove identificadores de sessão antes de versionar."""
import json
import pathlib
import sys

source, destination = map(pathlib.Path, sys.argv[1:3])
destination.mkdir(parents=True, exist_ok=True)
removed = {
    'responseContext', 'trackingParams', 'clickTrackingParams', 'loggingDirectives',
    'loggingContext', 'loggingConfig', 'loggingContext', 'frameworkUpdates', 'adPlacements',
    'watchEndpointSupportedOnesieConfig', 'playbackTracking', 'visitorData', 'serviceTrackingParams',
    'menu', 'menuRenderer', 'onLongPressCommand', 'accessibility', 'accessibilityContext',
    'styleRuns', 'attachmentRuns', 'thumbnailOverlays', 'thumbnailHoverOverlayViewModel',
    'thumbnailHoverOverlayToggleActionsViewModel', 'animatedThumbnailOverlayViewModel',
    'onResponseReceivedCommands', 'topbar', 'background', 'microformat',
    'themedPalette', 'menuButton', 'panelLoadingStrategy', 'inlineContent', 'buttons',
    'postRenderer', 'shareButton', 'subscribeButton', 'endScreen',
}

def sanitize(value, name, key=''):
    if isinstance(value, dict):
        result = {k: sanitize(v, name, k) for k, v in value.items() if k not in removed}
        if 'continuation' in result:
            result['continuation'] = 'CONT_' + name.replace('-', '_')
        if key == 'continuationCommand' and 'token' in result:
            result['token'] = 'CONT_' + name.replace('-', '_')
        return result
    if isinstance(value, list):
        limit = 8 if key in ('contents', 'items') else len(value)
        selected = value[:limit]
        if len(value) > limit:
            selected += [v for v in value[limit:] if isinstance(v, dict) and 'continuationItemRenderer' in v]
        return [sanitize(v, name) for v in selected]
    return value

for path in source.glob('*-raw.json'):
    name = path.name.removesuffix('-raw.json')
    out = sanitize(json.loads(path.read_text()), name)
    (destination / (name + '.json')).write_text(json.dumps(out, ensure_ascii=False, indent=2) + '\n')
for name in ('music', 'web'):
    (destination / (name + '-context.html')).write_text((source / (name + '-context.html')).read_text())
