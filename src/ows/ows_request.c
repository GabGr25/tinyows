/*
   Copyright (c) <2007-2009> <Barbara Philippot - Olivier Courtin>

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.
*/


#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlschemastypes.h>

#include "ows.h"
#include "../ows_define.h"


/*
 * Initialize ows_request structure
 */
ows_request *ows_request_init()
{
    ows_request *or;
    or = malloc(sizeof(ows_request));
    assert(or != NULL);

    or->version = NULL;
    or->service = OWS_SERVICE_UNKNOWN;
    or->method = OWS_METHOD_UNKNOWN;
    or->request.wfs = NULL;
    or->request.wms = NULL;

    return or;
}


/*
 * Release ows_request structure
 */
void ows_request_free(ows_request * or)
{
    assert(or != NULL);

    if (or->version != NULL)
        ows_version_free(or->version);

    switch (or->service) {
        case WFS:

            if (or->request.wfs != NULL)
                wfs_request_free(or->request.wfs);

            break;
        case WMS:

            if (or->request.wms != NULL)
                wms_request_free(or->request.wms);

            break;
        case OWS_SERVICE_UNKNOWN:
            break;
        default:
            assert(0);              /* Should not happen */
    }

    free(or);
    or = NULL;
}


#ifdef OWS_DEBUG
/*
 * Flush an ows_request structure
 * Used for debug purpose
 */
void ows_request_flush(ows_request * or, FILE * output)
{
    assert(or != NULL);
    assert(output != NULL);

    fprintf(output, "method:%d\n", or->method);
    fprintf(output, "service:%d\n", or->service);

    if (or->version != NULL) {
        fprintf(output, "version:");
        ows_version_flush(or->version, output);
        fprintf(output, "\n");
    }
}
#endif


/*
 * Simple callback used to catch error and warning from libxml2 schema
 * validation
 * If OWS_DEBUG mode output to stderr, else do nothing
 */
static void libxml2_callback  (void * ctx, const char * msg, ...) {
    va_list varg;
    char * str;
    ows *o;

    o = (ows *) ctx;
    va_start(varg, msg);
    str = (char *)va_arg( varg, char *);

    if (o->log != NULL) fprintf(o->log, "[ERROR] %s", str);
#ifdef OWS_DEBUG
    fprintf(stderr, "%s", str);
#endif
}


/*
 * Valid an xml string against an XML schema
 * Inpired from: http://xml.developpez.com/sources/?page=validation#validate_XSD_CppCLI_2
 */
int ows_schema_validation(const ows *o, buffer * xml_schema, buffer * xml, bool schema_is_file)
{
    xmlSchemaPtr schema;
    xmlSchemaParserCtxtPtr ctxt;
    xmlSchemaValidCtxtPtr validctxt;
    int ret;
    xmlDocPtr doc;

    assert(xml_schema != NULL);
    assert(xml != NULL);

    xmlInitParser();
    schema = NULL;
    ret = -1;

    /* Open XML Schema File */
    if (schema_is_file) ctxt = xmlSchemaNewParserCtxt(xml_schema->buf);
    else ctxt = xmlSchemaNewMemParserCtxt(xml_schema->buf, xml_schema->use);

    xmlSchemaSetParserErrors(ctxt,
                             (xmlSchemaValidityErrorFunc) libxml2_callback,
                             (xmlSchemaValidityWarningFunc) libxml2_callback, (void *) o);


    schema = xmlSchemaParse(ctxt);
    xmlSchemaFreeParserCtxt(ctxt);

    /* If XML Schema hasn't been rightly loaded */
    if (schema == NULL) {
        xmlSchemaCleanupTypes();
        xmlMemoryDump();
        xmlCleanupParser();
        return ret;
    }

    doc = xmlParseMemory(xml->buf, xml->use);

    if (doc != NULL) {
        /* Loading XML Schema content */
        validctxt = xmlSchemaNewValidCtxt(schema);
        xmlSchemaSetValidErrors(validctxt,
                                (xmlSchemaValidityErrorFunc) libxml2_callback,
                                (xmlSchemaValidityWarningFunc) libxml2_callback, (void *) o);
        /* validation */
        ret = xmlSchemaValidateDoc(validctxt, doc);
        xmlSchemaFreeValidCtxt(validctxt);
    }

    xmlSchemaFree(schema);
    xmlFreeDoc(doc);
    xmlCleanupParser();

    return ret;
}


/*
 * Check and fill version
 */
static ows_version *ows_request_check_version(ows * o, ows_request * or,
        const array * cgi)
{
    buffer *b;
    list *l;
    ows_version *v;

    assert(o != NULL);
    assert(or != NULL);
    assert(cgi != NULL);

    l = NULL;

    v = or->version;

    assert(o != NULL);
    assert(or != NULL);
    assert(cgi != NULL);

    b = array_get(cgi, "version");

    if (buffer_cmp(b, "")) {
        ows_version_set(o->request->version, 0, 0, 0);
    } else {
        /* check if version format is x.y.z */
        if (b->use < 5)
            ows_error(o, OWS_ERROR_INVALID_PARAMETER_VALUE,
                      "VERSION parameter is not valid (use x.y.z)", "version");

        l = list_explode('.', b);

        if (l->size != 3) {
            list_free(l);
            ows_error(o, OWS_ERROR_INVALID_PARAMETER_VALUE,
                      "VERSION parameter is not valid (use x.y.z)", "version");
        }

        if (check_regexp(l->first->value->buf, "^[0-9]+$")
                && check_regexp(l->first->next->value->buf, "^[0-9]+$")
                && check_regexp(l->first->next->next->value->buf, "^[0-9]+$")) {
            v->major = atoi(l->first->value->buf);
            v->minor = atoi(l->first->next->value->buf);
            v->release = atoi(l->first->next->next->value->buf);
        } else {
            list_free(l);
            ows_error(o, OWS_ERROR_INVALID_PARAMETER_VALUE,
                      "VERSION parameter is not valid (use x.y.z)", "version");
        }

        list_free(l);
    }

    return v;
}


/*
 * Check common OWS parameters's validity
 *(service, version, request and layers definition)
 */
void ows_request_check(ows * o, ows_request * or, const array * cgi,
                       const char *query)
{
    buffer *b, *typename, *xmlstring;
    ows_layer_node *ln;
    list_node *srid;
    bool srsname;
    int valid;
    buffer *schema;

    assert(o != NULL);
    assert(or != NULL);
    assert(cgi != NULL);
    assert(query != NULL);

    b = NULL;
    ln = NULL;
    srsname = false;
    valid = 0;

    /* check if SERVICE is set */
    if (!array_is_key(cgi, "service")) {
        /* Tests WFS 1.1.0 require a default value for requests
           encoded in XML if service is not set */
        if (cgi_method_get())
            ows_error(o, OWS_ERROR_MISSING_PARAMETER_VALUE,
                      "SERVICE is not set", "SERVICE");
        else {
            if (buffer_case_cmp(o->metadata->type, "WMS"))
                or->service = WMS;
            else if (buffer_case_cmp(o->metadata->type, "WFS"))
                or->service = WFS;
            else
                ows_error(o, OWS_ERROR_INVALID_PARAMETER_VALUE,
                          "service unknown", "service");
        }
    } else {
        b = array_get(cgi, "service");

        if (buffer_case_cmp(b, "WMS"))
            or->service = WMS;
        else if (buffer_case_cmp(b, "WFS"))
            or->service = WFS;
        else if (buffer_case_cmp(b, "WCS"))
            ows_error(o, OWS_ERROR_INVALID_PARAMETER_VALUE,
                      "service not implemented", "service");
        else if (buffer_case_cmp(b, "")) {
            if (buffer_case_cmp(o->metadata->type, "WMS"))
                or->service = WMS;
            else if (buffer_case_cmp(o->metadata->type, "WFS"))
                or->service = WFS;
            else
                ows_error(o, OWS_ERROR_INVALID_PARAMETER_VALUE,
                          "service unknown", "service");
        } else
            ows_error(o, OWS_ERROR_INVALID_PARAMETER_VALUE,
                      "service unknown", "service");
    }


    /* check if REQUEST is set */
    if (!array_is_key(cgi, "request"))
        ows_error(o, OWS_ERROR_MISSING_PARAMETER_VALUE,
                  "REQUEST is not set", "REQUEST");
    else
        b = array_get(cgi, "request");

    /* check if VERSION is set and init Version */
    or->version = ows_version_init();

    if (!array_is_key(cgi, "version") || buffer_cmp(array_get(cgi, "version"), "")) {
        if (!buffer_case_cmp(b, "GetCapabilities")) {
            /* WFS 1.1.0 with KVP need a version set */
            if (o->request->method == OWS_METHOD_KVP)
                ows_error(o, OWS_ERROR_MISSING_PARAMETER_VALUE,
                          "VERSION is not set", "VERSION");
            /* WFS 1.1.0 require a default value for requests
               encoded in XML if version is not set */
            else if (o->request->method == OWS_METHOD_XML) {
                if (or->service == WFS)
                    ows_version_set(o->request->version, 1, 1, 0);
                else if (or->service == WMS)
                    ows_version_set(o->request->version, 1, 3, 0);
            }
        }
    } else or->version = ows_request_check_version(o, or, cgi);

    /* check if layers have name or title and srs */
    for (ln = o->layers->first; ln != NULL; ln = ln->next) {
        if (ln->layer->name == NULL)
            ows_error(o, OWS_ERROR_CONFIG_FILE,
                      "No layer name defined", "config_file");

        if (ows_layer_match_table(o, ln->layer->name)) {
            if (ln->layer->title == NULL)
                ows_error(o, OWS_ERROR_CONFIG_FILE,
                          "No layer title defined", "config_file");

            if (or->service == WFS) {
                if (ln->layer->prefix == NULL)
                    ows_error(o, OWS_ERROR_CONFIG_FILE,
                              "No layer prefix defined", "config_file");

                if (ln->layer->server == NULL)
                    ows_error(o, OWS_ERROR_CONFIG_FILE,
                              "No layer server defined", "config_file");
            }

            if (ln->layer->srid != NULL) {
                if (array_is_key(cgi, "srsname")
                        && array_is_key(cgi, "typename")) {
                    b = array_get(cgi, "srsname");
                    typename = array_get(cgi, "typename");

                    if (buffer_cmp(typename, ln->layer->name->buf)) {

                        if (!check_regexp(b->buf, "^http://www.opengis.net")
                                && !check_regexp(b->buf, "^EPSG")
                                && !check_regexp(b->buf, "^urn:"))
                            ows_error(o, OWS_ERROR_CONFIG_FILE,
                                      "srsname isn't valid", "srsName");

                        for (srid = ln->layer->srid->first; srid != NULL;
                                srid = srid->next) {
                            if (check_regexp(b->buf, srid->value->buf))
                                srsname = true;
                        }

                        if (srsname == false)
                            ows_error(o, OWS_ERROR_CONFIG_FILE,
                                      "srsname doesn't match srid",
                                      "config_file");
                    }
                }
            }
        }
    }

    /* check XML Validity */
    if ((cgi_method_post() && strcmp(getenv("CONTENT_TYPE"), "application/x-www-form-urlencoded") != 0)
            || (!cgi_method_post() && !cgi_method_get() && query[0] == '<')) {

        if (or->service == WFS && o->check_schema) {

            xmlstring = buffer_init();
            buffer_add_str(xmlstring, query);

            if (ows_version_get(or->version) == 100) {
                if (buffer_cmp(b, "Transaction")) {
                    schema = wfs_generate_schema(o);
                    valid = ows_schema_validation(o, schema, xmlstring, false);
                } else {
                    schema = buffer_init();
                    buffer_copy(schema, o->schema_dir);
                    buffer_add_str(schema, WFS_SCHEMA_100_BASIC);
                    valid = ows_schema_validation(o, schema, xmlstring, true);
                }
            } else {
                if (buffer_cmp(b, "Transaction")) {
                   schema = wfs_generate_schema(o);
                   valid = ows_schema_validation(o, schema, xmlstring, false);
                } else {
                   schema = buffer_init();
                   buffer_copy(schema, o->schema_dir);
                   buffer_add_str(schema, WFS_SCHEMA_110);
                   valid = ows_schema_validation(o, schema, xmlstring, true);
                }
            }

            buffer_free(schema);
            buffer_free(xmlstring);
            
            if (valid != 0)
                ows_error(o, OWS_ERROR_INVALID_PARAMETER_VALUE,
                      "xml isn't valid", "request");
        }
    }
}


/*
 * vim: expandtab sw=4 ts=4
 */