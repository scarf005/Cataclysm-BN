---
title: Translation API
---

## Grammatical gender

For NPC dialogue (and potentially other strings) some languages may wish to have alternate
translations depending on the gender of the conversation participants. This two pieces of initial
configuration.

1. The dialogue must have the relevant genders listed in the json file defining it. See
   [the NPC docs](https://github.com/cataclysmbnteam/Cataclysm-BN/blob/upload/doc/NPCs.md).
2. Each language must specify the genders it wishes to use via `genders` list of the language's
   entry in `data/raw/languages.json`. Don't add genders there until you're sure you will need them,
   because it will make more work for you. Current choices are: `m` (male), `f` (female), `n`
   (neuter). If you need different genders than the ones currently supported, see relevant note in
   `src/language.h`.

Having done this, the relevant dialogue lines will appear multiple times for translation, with
different genders specified in the message context. For example, a context of `npc:m` would indicate
that the NPC participant in the conversation is male.

Because of technical limitations, all supported genders will appear as contexts, but you only need
to provide translations for the genders listed in the grammatical gender list for your language.

Other parts of the game have various ad hoc solutions to grammatical gender, so don't be surprised
to see other contexts appearing for other strings.

### Tips

There are issues specific to Cataclysm: BN which translators should be aware of. These include the
use of terms like `%s` and `%3$d` (leave them as they are), and the use of tags like `<name>`, which
shouldn't be translated.

Information about these and any other issues specific to individual languages, can be found in
Cataclysm: BN's [language notes folder][notes].

General notes for all translators are in `README_all_translators.txt`, and notes specific to a
language may be stored as `<lang_id>.txt`, for example `de.txt` for German.

Cataclysm: BN has more than 14000 translatable strings, but don't be discouraged. The more
translators there are, the easier it becomes 😄.

## Developers

Cataclysm: BN uses custom runtime library that works similarly to [GNU gettext][gettext] to display
translated texts.

Using `gettext` requires two actions:

- Marking strings that should be translated in the source code.
- Calling translation functions at run time.

Marking translatable string allows for their automatic extraction. This process generates a file
that maps the original string (usually in English) as it appears in the source code to the
translated string. These mappings are used at run time by the translation functions.

Note that only extracted strings can get translated, since the original string is acting as the
identifier used to request the translation. If a translation function can't find the translation, it
returns the original string.

## Translation Functions

In order to mark a string for translation and to obtain its translation at runtime, you should use
one of the following functions and classes.

String _literals_ that are used in any of these functions are automatically extracted. Non-literal
strings are still translated at run time, but they won't get extracted.

### `_()`

This function is appropriate for use on simple strings, for example:

```cpp
const char *translated = _( "text marked for translation" )
```

It also works directly:

```cpp
add_msg( _( "You drop the %s." ), the_item_name );
```

Strings from the JSON files are extracted by the `lang/extract_json_strings.py` script, and can be
translated at run time using `_()`. If translation context is desired for a JSON string,
`class translation` can be used instead, which is documented below.

### `pgettext()`

This function is useful when the original string's meaning is ambiguous in isolation. For example,
the word "blue", which can mean either a color or an emotion.

In addition to the translatable string, `pgettext` receives a context which is provided to the
translators, but is not part of the translated string itself. This function's first parameter is the
context, the second is the string to be translated:

```cpp
const char *translated = pgettext( "The color", "blue" );
```

### `vgettext()`

Some languages have complex rules for plural forms. `vgettext` can be used to translate these
plurals correctly. Its first parameter is the untranslated string in singular form, the second
parameter is the untranslated string in plural form and the third one is used to determine which one
of the first two should be used at run time:

```cpp
const char *translated = vgettext( "%d zombie", "%d zombies", num_of_zombies );
```

### `vpgettext()`

Same as `vgettext`, but allows to specify translation context.

```cpp
const char *translated = vpgettext( "water source, not time of year", "%d spring", "%d springs", num_of_springs );
```

## `translation`

There are times when you want to store a string for translation, maybe with translation context;
Sometimes you may also want to store a string that needs no translation or has plural forms.
`class translation` in `translations.h|cpp` offers these functionalities in a single wrapper:

```cpp
const translation text = to_translation( "Context", "Text" );
```

```cpp
const translation text = to_translation( "Text without context" );
```

```cpp
const translation text = pl_translation( "Singular", "Plural" );
```

```cpp
const translation text = pl_translation( "Context", "Singular", "Plural" );
```

```cpp
const translation text = no_translation( "This string will not be translated" );
```

The string can then be translated/retrieved with the following code

```cpp
const std::string translated = text.translated();
```

```cpp
// this translates the plural form of the text corresponding to the number 2
const std::string translated = text.translated( 2 );
```

`class translation` can also be read from JSON. The method `translation::deserialize()` handles
deserialization from a `JsonIn` object, so translations can be read from JSON using the appropriate
JSON functions. The JSON syntax is as follows:

```json
"name": "bar"
```

```json
"name": { "ctxt": "foo", "str": "bar", "str_pl": "baz" }
```

or

```json
"name": { "ctxt": "foo", "str_sp": "foo" }
```

In the above code, `"ctxt"` and `"str_pl"` are both optional, whereas `"str_sp"` is equivalent to
specifying `"str"` and `"str_pl"` with the same string. Additionally, `"str_pl"` and `"str_sp"` will
only be read if the translation object is constructed using `plural_tag` or `pl_translation()`, or
converted using `make_plural()`. Here's an example:

```cpp
translation name{ translation::plural_tag() };
jsobj.read( "name", name );
```

If neither `"str_pl"` nor `"str_sp"` is specified, the plural form defaults to the singular form +
"s".

You can also add comments for translators by writing it like below (the order of the entries does
not matter):

```json
"name": {
    "//~": "as in 'foobar'",
    "str": "bar"
}
```

Do note that currently the JSON syntax is only supported for some JSON values, which are listed
below. If you want other json strings to use this format, refer to `translations.h|cpp` and migrate
the corresponding code. Afterwards you may also want to test `update_pot.sh` to ensure that the
strings are correctly extracted for translation, and run the unit test to fix text styling issues
reported by the `translation` class.

### Supported JSON values

- Effect names
- Item action names
- Item category names
- Activity verbs
- Gate action messages
- Spell names and descriptions
- Terrain/furniture descriptions
- Monster melee attack messages
- Morale effect descriptions
- Mutation names/descriptions
- NPC class names/descriptions
- Tool quality names
- Score descriptions
- Skill names/descriptions
- Bionic names/descriptions
- Terrain bash sound descriptions
- Trap-vehicle collision sound descriptions
- Vehicle part names/descriptions
- Skill display type names
- NPC dialogue u_buy_monster unique names
- Spell messages and monster spell messages
- Martial art names and descriptions
- Mission names and descriptions
- Fault names and descriptions
- Plant names in item seed data
- Transform use action messages and menu text
- Template NPC names and name suffixes
- NPC talk response text
- Relic name overrides
- Relic recharge messages
- Speech text
- Tutorial messages
- Vitamin names
- Recipe blueprint names
- Recipe group recipe descriptions
- Item names (plural supported) and descriptions
- Recipe descriptions
- Inscribe use action verbs/gerunds
- Monster names (plural supported) and descriptions
- Snippets
- Bodypart names
- Keybinding action names
- Field level names

### Lua

The 4 translation functions are exposed to the Lua code. See
[LUA_SUPPORT.md](../../../../../../doc/LUA_SUPPORT.md) for details.

### Recommendations

In Cataclysm: BN, some classes, like `itype` and `mtype`, provide a wrapper for the translation
functions, called `nname`.

When an empty string is marked for translation, it is always translated into debug information,
rather than an empty string. On most cases, strings can be considered to be never empty, and thus
always safe to mark for translation, however, when handling a string that can be empty and _needs_
to remain empty after translation, the string should be checked for emptiness and only passed to a
translation function when is non-empty.

Error and debug messages must not be marked for translation. When they appear, the player is
expected to report them _exactly_ as they are printed by the game.

See the [gettext manual][manual] for more information.

[notes]: https://github.com/cataclysmbnteam/Cataclysm-BN/tree/upload/lang/notes
[gettext]: https://www.gnu.org/software/gettext/
[manual]: https://www.gnu.org/software/gettext/manual/index.html
