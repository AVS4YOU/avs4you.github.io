# AVS4YOU Plugins Documentation

Welcome to the official documentation for developing plugins for AVS4YOU applications.

## Plugin Types

We support two types of plugins for extending AVS4YOU application functionality:

### [Content Plugins](ContentPlugin-README.md)
Plugins that provide menu items, UI elements, and context-specific functionality. Content plugins can add custom menu items to AVS4YOU application context menus, create custom dialogs, and interact with users.

### [Effect Plugins](EffectPlugin-README.md)
Plugins that provide various effects for image or video processing. Effect plugins can apply filters, transformations, and other visual effects to media content in AVS4YOU applications.

## Guides

### [Effect Plugin Recipes](EffectPlugin-Recipes.md)
How to write an animated effect that actually looks right: temporal coherence
(making an animation continuous from frame to frame instead of random per
frame), resolution scaling rules, making an overlay read on any footage,
performance budgets, and the build/preview/verify pipeline.

### [Plugin Manifest](PluginsManifest.md)
The format of `plugins.json`, the machine-readable catalogue AVS4YOU installers
read to download their preinstalled plugins: required fields, what `pluginId`
must match, when the schema changes, and the order to publish a new plugin
version in.

## Getting Started

1. Choose the plugin type that matches your needs
2. Follow the developer guide for detailed implementation instructions
3. Use the provided SDK and examples to start building your plugin

## Resources

- [Visit Marketplace](https://avs4you.github.io) - Browse and download available plugins
- [GitHub Repository](https://github.com/avs4you/avs4you.github.io) - View source code and contribute

---

For questions and support, please visit [AVS4YOU.com](https://www.avs4you.com)
