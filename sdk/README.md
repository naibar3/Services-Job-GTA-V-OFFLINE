# Script Hook V SDK (not included)

The SDK's own readme says it **must not be redistributed**, so it is not part of this repository or the source package.

1. Download the SDK from <http://www.dev-c.com/gtav/scripthookv/> ("Script Hook V SDK").
2. Put these files here so the layout is:

```
sdk/
  inc/    enums.h  main.h  nativeCaller.h  natives.h  types.h
  lib/    ScriptHookV.lib
```

`build.ps1` includes `-I sdk` and links `sdk\lib\ScriptHookV.lib`.

Terms from the SDK readme: scripts built with it must be intended to work **offline**, and `ScriptHookV.dll`
must not be redistributed with your plugin (link to the official download instead).
