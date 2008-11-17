#include <libxml/xmlreader.h>

#include "rest-private.h"
#include "rest-xml-parser.h"

G_DEFINE_TYPE (RestXmlParser, rest_xml_parser, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), REST_TYPE_XML_PARSER, RestXmlParserPrivate))

#define G(x) (gchar *)x

typedef struct _RestXmlParserPrivate RestXmlParserPrivate;

struct _RestXmlParserPrivate {
  xmlTextReaderPtr reader;
};

static void
rest_xml_parser_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
rest_xml_parser_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
rest_xml_parser_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (rest_xml_parser_parent_class)->dispose)
    G_OBJECT_CLASS (rest_xml_parser_parent_class)->dispose (object);
}

static void
rest_xml_parser_finalize (GObject *object)
{
  RestXmlParserPrivate *priv = GET_PRIVATE (object);

  xmlFreeTextReader (priv->reader);

  if (G_OBJECT_CLASS (rest_xml_parser_parent_class)->finalize)
    G_OBJECT_CLASS (rest_xml_parser_parent_class)->finalize (object);
}

static void
rest_xml_parser_class_init (RestXmlParserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (RestXmlParserPrivate));

  object_class->get_property = rest_xml_parser_get_property;
  object_class->set_property = rest_xml_parser_set_property;
  object_class->dispose = rest_xml_parser_dispose;
  object_class->finalize = rest_xml_parser_finalize;
}

static void
rest_xml_parser_init (RestXmlParser *self)
{
}

static RestXmlNode *
rest_xml_node_reverse_siblings (RestXmlNode *current)
{
  RestXmlNode *next;
  RestXmlNode *prev = NULL;

  while (current)
  {
    next = current->next;
    current->next = prev;

    prev = current;
    current = next;
  }

  return prev;
}

static void
rest_xml_node_reverse_children_siblings (RestXmlNode *node)
{
  GList *l, *children;
  RestXmlNode *new_node;

  children = g_hash_table_get_values (node->children);

  for (l = children; l; l = l->next)
  {
    new_node = rest_xml_node_reverse_siblings ((RestXmlNode *)l->data);
    g_hash_table_insert (node->children, new_node->name, new_node);
  }

  if (children)
    g_list_free (children);
}

static RestXmlNode *
rest_xml_node_prepend (RestXmlNode *cur_node, RestXmlNode *new_node)
{
  g_assert (new_node->next == NULL);
  new_node->next = cur_node;

  return new_node;
}

RestXmlNode *
rest_xml_node_new ()
{
  RestXmlNode *node;

  node = g_slice_new0 (RestXmlNode);
  node->children = g_hash_table_new (NULL, NULL);
  node->attrs = g_hash_table_new_full (g_str_hash,
                                       g_str_equal,
                                       g_free,
                                       g_free);

  return node;
}

void
rest_xml_node_free (RestXmlNode *node)
{
  GList *l;

  l = g_hash_table_get_values (node->children);
  while (l)
  {
    rest_xml_node_free ((RestXmlNode *)l->data);
    l = g_list_delete_link (l, l);
  }

  g_hash_table_unref (node->children);
  g_hash_table_unref (node->attrs);
  g_free (node->content);
  g_slice_free (RestXmlNode, node);
}

const gchar *
rest_xml_node_get_attr (RestXmlNode *node, 
                        const gchar *attr_name)
{
  return g_hash_table_lookup (node->attrs, attr_name);
}

RestXmlNode *
rest_xml_node_find (RestXmlNode *start,
                    const gchar *tag)
{
  RestXmlNode *node;
  RestXmlNode *tmp;
  GQueue stack = G_QUEUE_INIT;
  GList *children, *l;
  const char *tag_interned;

  tag_interned = g_intern_string (tag);

  g_queue_push_head (&stack, start);

  while ((node = g_queue_pop_head (&stack)) != NULL)
  {
    if ((tmp = g_hash_table_lookup (node->children, tag_interned)) != NULL)
    {
      return tmp;
    }

    children = g_hash_table_get_values (node->children);
    for (l = children; l; l = l->next)
    {
      g_queue_push_head (&stack, l->data);
    }
    g_list_free (children);
  }

  return NULL;
}

RestXmlParser *
rest_xml_parser_new (void)
{
  return g_object_new (REST_TYPE_XML_PARSER, NULL);
}

RestXmlNode *
rest_xml_parser_parse_from_data (RestXmlParser *parser, 
                                 const gchar   *data,
                                 goffset        len)
{
  RestXmlParserPrivate *priv = GET_PRIVATE (parser);
  RestXmlNode *cur_node = NULL;
  RestXmlNode *new_node = NULL;
  RestXmlNode *tmp_node = NULL;
  RestXmlNode *root_node = NULL;
  RestXmlNode *node = NULL;

  const gchar *name = NULL;
  const gchar *attr_name = NULL;
  const gchar *attr_value = NULL;
  GQueue nodes = G_QUEUE_INIT;
  gint res = 0;

  _rest_setup_debugging ();

  priv->reader = xmlReaderForMemory (data,
                                     len,
                                     NULL, /* URL? */
                                     NULL, /* encoding */
                                     XML_PARSE_RECOVER | XML_PARSE_NOCDATA);

  while ((res = xmlTextReaderRead (priv->reader)) == 1)
  {
    switch (xmlTextReaderNodeType (priv->reader))
    {
      case XML_READER_TYPE_ELEMENT:
        /* Lookup the "name" for the tag */
        name = G(xmlTextReaderConstName (priv->reader));
        REST_DEBUG (XML_PARSER, "Opening tag: %s", name);

        /* Create our new node for this tag */

        new_node = rest_xml_node_new ();
        new_node->name = G (g_intern_string (name));

        if (!root_node)
        {
          root_node = new_node;
        }

        /* 
         * Check if we are not the root node because we need to update it's
         * children set to include the new node.
         */
        if (cur_node)
        {
          tmp_node = g_hash_table_lookup (cur_node->children, new_node->name);

          if (tmp_node)
          {
            REST_DEBUG (XML_PARSER, "Existing node found for this name. "
                              "Prepending to the list.");
            g_hash_table_insert (cur_node->children, 
                                 G(tmp_node->name),
                                 rest_xml_node_prepend (tmp_node, new_node));
          } else {
            REST_DEBUG (XML_PARSER, "Unseen name. Adding to the children table.");
            g_hash_table_insert (cur_node->children,
                                 G(new_node->name),
                                 new_node);
          }
        }

        /* 
         * Check for empty element. If empty we needn't worry about children
         * or text and thus we don't need to update the stack or state
         */
        if (xmlTextReaderIsEmptyElement (priv->reader)) 
        {
          REST_DEBUG (XML_PARSER, "We have an empty element. No children or text.");
        } else {
          REST_DEBUG (XML_PARSER, "Non-empty element found."
                            "  Pushing to stack and updating current state.");
          g_queue_push_head (&nodes, new_node);
          cur_node = new_node;
        }

        /* 
         * Check if we have attributes. These get stored in the node's attrs
         * hash table.
         */
        if (xmlTextReaderHasAttributes (priv->reader))
        {
          xmlTextReaderMoveToFirstAttribute (priv->reader);

          do {
            attr_name = G(xmlTextReaderConstLocalName (priv->reader));
            attr_value = G(xmlTextReaderConstValue (priv->reader));
            g_hash_table_insert (new_node->attrs,
                                 g_strdup (attr_name),
                                 g_strdup (attr_value));

            REST_DEBUG (XML_PARSER, "Attribute found: %s = %s",
                     attr_name, 
                     attr_value);

          } while ((res = xmlTextReaderMoveToNextAttribute (priv->reader)) == 1);
        }

        break;
      case XML_READER_TYPE_END_ELEMENT:
        REST_DEBUG (XML_PARSER, "Closing tag: %s", 
                 xmlTextReaderConstLocalName (priv->reader));

        REST_DEBUG (XML_PARSER, "Popping from stack and updating state.");

        /* For those children that have siblings, reverse the siblings */
        node = (RestXmlNode *)g_queue_pop_head (&nodes);
        rest_xml_node_reverse_children_siblings (node);

        /* Update the current node to the new top of the stack */
        cur_node = (RestXmlNode *)g_queue_peek_head (&nodes);

        if (cur_node)
        {
          REST_DEBUG (XML_PARSER, "Head is now %s", cur_node->name);
        } else {
          REST_DEBUG (XML_PARSER, "At the top level");
        }
        break;
      case XML_READER_TYPE_TEXT:
        cur_node->content = g_strdup (G(xmlTextReaderConstValue (priv->reader)));
        REST_DEBUG (XML_PARSER, "Text content found: %s",
                 cur_node->content);
      default:
        REST_DEBUG (XML_PARSER, "Found unknown content with type: 0x%x", 
                 xmlTextReaderNodeType (priv->reader));
        break;
    }
  }

  xmlTextReaderClose (priv->reader);
  return root_node;
}


