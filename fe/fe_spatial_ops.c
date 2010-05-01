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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../ows/ows.h"


/*
 * Check if the string is a spatial operator
 */
bool fe_is_spatial_op(char *name)
{
    assert(name != NULL);

    /* case sensitive comparison because the gml standard specifies
       strictly the name of the operator */
    if (strcmp(name, "Equals") == 0
            || strcmp(name, "Disjoint") == 0
            || strcmp(name, "Touches") == 0
            || strcmp(name, "Within") == 0
            || strcmp(name, "Overlaps") == 0
            || strcmp(name, "Crosses") == 0
            || strcmp(name, "Intersects") == 0
            || strcmp(name, "Contains") == 0
            || strcmp(name, "DWithin") == 0
            || strcmp(name, "Beyond") == 0
	    || strcmp(name, "BBOX") == 0)
        return true;

    return false;
}


/*
 * Transform syntax coordinates from GML 2.1.2 (x1,y1 x2,y2) into Postgis (x1 y1,x2 y2)
 */
static  buffer *fe_transform_coord_gml_to_psql(buffer * coord)
{
    size_t i;
    assert(coord != NULL);

    /*check if the first separator is a comma else do nothing */
    if (check_regexp(coord->buf, "^[0-9.-]+,")) {
        for (i = 0; i < coord->use; i++) {
            if (coord->buf[i] == ' ')       coord->buf[i] = ',';
            else if (coord->buf[i] == ',')  coord->buf[i] = ' ';
        }
    }

    return coord;
}


/*
 * Write a polygon geometry according to postgresql syntax from GML bbox
 */
buffer *fe_envelope(ows * o, buffer * typename, filter_encoding * fe, xmlNodePtr n)
{
    xmlChar *content, *srsname;
    buffer *srid, *name, *tmp;
    list *coord_min, *coord_max, *coord_pair;
    int srid_int;

    assert(o != NULL);
    assert(typename != NULL);
    assert(fe != NULL);
    assert(n != NULL);

    name = buffer_init();
    buffer_add_str(name, (char *) n->name);

    if (o->request->request.wfs->srs != NULL)
        srid_int = o->request->request.wfs->srs->srid;
    else
        srid_int = ows_srs_get_srid_from_layer(o, typename);

    srid = buffer_init();
    buffer_add_int(srid, srid_int);
    srsname = xmlGetProp(n, (xmlChar *) "srsName");

    /* BBOX is transformed into a polygon so that point corners are included */
    buffer_add_str(fe->sql, "setsrid('BOX(");

    if (srsname != NULL) {
        if (!check_regexp((char *) srsname, srid->buf)) {
            fe->error_code = FE_ERROR_SRS;
            xmlFree(srsname);
            buffer_free(name);
            buffer_free(srid);
            return fe->sql;
        }
    }

    n = n->children;

    /* jump to the next element if there are spaces */
    while (n->type != XML_ELEMENT_NODE)
        n = n->next;

    content = xmlNodeGetContent(n->children);

    /* GML3 */
    if (buffer_cmp(name, "Envelope")) {
        coord_min = list_explode_str(' ', (char *) content);

        n = n->next;

        /* jump to the next element if there are spaces */
        while (n->type != XML_ELEMENT_NODE)
            n = n->next;

        xmlFree(content);
        content = xmlNodeGetContent(n->children);

        coord_max = list_explode_str(' ', (char *) content);
    }
    /* GML2 */
    else {
        tmp = buffer_init();
        buffer_add_str(tmp, (char *) content);

        tmp = fe_transform_coord_gml_to_psql(tmp);

        coord_pair = list_explode(',', tmp);
        coord_min = list_explode(' ', coord_pair->first->value);
        coord_max = list_explode(' ', coord_pair->first->next->value);
        buffer_free(tmp);
        list_free(coord_pair);
    }

    /* display the polygon's coordinates matching the bbox */

    buffer_copy(fe->sql, coord_min->first->value);
    buffer_add_str(fe->sql, " ");
    buffer_copy(fe->sql, coord_min->first->next->value);
    buffer_add_str(fe->sql, ",");
    buffer_copy(fe->sql, coord_max->first->value);
    buffer_add_str(fe->sql, " ");
    buffer_copy(fe->sql, coord_max->first->next->value);

    list_free(coord_min);
    list_free(coord_max);

    buffer_add_str(fe->sql, ")'::box2d,");

    buffer_copy(fe->sql, srid);
    buffer_add_str(fe->sql, ")");

    xmlFree(srsname);
    xmlFree(content);
    buffer_free(srid);
    buffer_free(name);

    return fe->sql;
}


/*
 * Return the SQL request matching the spatial operator
 */
static buffer *fe_spatial_functions(ows * o, buffer * typename,
                                    filter_encoding * fe, xmlNodePtr n)
{
    bool transform = false;
    buffer *sql;

    assert(o != NULL);
    assert(typename != NULL);
    assert(fe != NULL);
    assert(n != NULL);

    if (strcmp((char *) n->name, "Equals") == 0)
        buffer_add_str(fe->sql, " ST_Equals(");

    if (strcmp((char *) n->name, "Disjoint") == 0)
        buffer_add_str(fe->sql, " ST_Disjoint(");

    if (strcmp((char *) n->name, "Touches") == 0)
        buffer_add_str(fe->sql, " ST_Touches(");

    if (strcmp((char *) n->name, "Within") == 0)
        buffer_add_str(fe->sql, " ST_Within(");

    if (strcmp((char *) n->name, "Overlaps") == 0)
        buffer_add_str(fe->sql, " ST_Overlaps(");

    if (strcmp((char *) n->name, "Crosses") == 0)
        buffer_add_str(fe->sql, " ST_Crosses(");

    if (strcmp((char *) n->name, "Intersects") == 0)
        buffer_add_str(fe->sql, " ST_Intersects(");

    if (strcmp((char *) n->name, "Contains") == 0)
        buffer_add_str(fe->sql, " ST_Contains(");

    n = n->children;

    /* jump to the next element if there are spaces */
    while (n->type != XML_ELEMENT_NODE) n = n->next;

    /* FIXME: add schema name */
    if (o->request->request.wfs->srs != NULL) {
        transform = true;
        buffer_add_str(fe->sql, "st_transform(");
    }

    fe->sql = fe_property_name(o, typename, fe, fe->sql, n, true);

    if (transform) {
        buffer_add(fe->sql, ',');
        buffer_add_int(fe->sql, o->request->request.wfs->srs->srid);
        buffer_add(fe->sql, ')');
    }
        
    n = n->next;

    /* jump to the next element if there are spaces */
    while (n->type != XML_ELEMENT_NODE) n = n->next;

    buffer_add_str(fe->sql, ",'");

    if (strcmp((char *) n->name, "Box") == 0
            || strcmp((char *) n->name, "Envelope") == 0)
        fe->sql = fe_envelope(o, typename, fe, n);
    else  {
        sql = ows_psql_gml_to_sql(o, n);
        if (sql != NULL) { 
            buffer_copy(fe->sql, sql);
            buffer_free(sql);
        } /* TODO else case */
    }

    buffer_add_str(fe->sql, "')");

    return fe->sql;
}


/*
 * DWithin and Beyond operators : test if a geometry A is within (or beyond)
 * a specified distance of a geometry B
 */
static buffer *fe_distance_functions(ows * o, buffer * typename,
                                     filter_encoding * fe, xmlNodePtr n)
{
    xmlChar *content, *units;
    buffer *tmp, *op, *sql;
    float km;

    assert(o != NULL);
    assert(typename != NULL);
    assert(fe != NULL);
    assert(n != NULL);

    tmp = NULL;
    op = buffer_init();

    if (strcmp((char *) n->name, "Beyond") == 0)
        buffer_add_str(op, " > ");

    if (strcmp((char *) n->name, "DWithin") == 0)
        buffer_add_str(op, " < ");

    /* parameters are passed with centroid function because
       Distance_sphere parameters must be points
       So to be able to calculate distance between lines or polygons
       whose coordinates are degree, centroid function must be used
       To be coherent, centroid is also used with Distance function */
    if (ows_srs_meter_units(o, typename))
        buffer_add_str(fe->sql, "ST_Distance(ST_centroid(");
    else
        buffer_add_str(fe->sql, "ST_Distance_sphere(ST_centroid(");

    n = n->children;

    /* jump to the next element if there are spaces */
    while (n->type != XML_ELEMENT_NODE)
        n = n->next;

    /* display the property name */
    fe->sql = fe_property_name(o, typename, fe, fe->sql, n, true);

    buffer_add_str(fe->sql, "),ST_centroid('");

    n = n->next;

    while (n->type != XML_ELEMENT_NODE)
        n = n->next;

    /* display the geometry */
    sql = ows_psql_gml_to_sql(o, n);
    if (sql != NULL) {
        buffer_copy(fe->sql, sql);
        buffer_free(sql);
    } /* TODO else case */

    buffer_add_str(fe->sql, "'))");

    n = n->next;

    while (n->type != XML_ELEMENT_NODE)
        n = n->next;

    units = xmlGetProp(n, (xmlChar *) "units");
    buffer_copy(fe->sql, op);
    content = xmlNodeGetContent(n->children);

    /* units not strictly defined in Filter Encoding specification */
    if (strcmp((char *) units, "meters") == 0
            || strcmp((char *) units, "#metre") == 0)
        buffer_add_str(fe->sql, (char *) content);
    else if (strcmp((char *) units, "kilometers") == 0
             || strcmp((char *) units, "#kilometre") == 0) {
        km = atof((char *) content) * 1000.0;
        tmp = buffer_ftoa((double) km);
        buffer_copy(fe->sql, tmp);
        buffer_free(tmp);
    } else {
        fe->error_code = FE_ERROR_UNITS;
    }

    buffer_free(op);
    xmlFree(content);
    xmlFree(units);

    return fe->sql;
}


/*
 * BBOX operator : identify all geometries that spatially interact with the specified box
 */
static buffer *fe_bbox(ows * o, buffer * typename, filter_encoding * fe,
                       xmlNodePtr n)
{
    bool transform = false;

    assert(o != NULL);
    assert(typename != NULL);
    assert(fe != NULL);
    assert(n != NULL);

    buffer_add_str(fe->sql, "st_intersects(");

    if (o->request->request.wfs->srs != NULL) {
        transform = true;
        buffer_add_str(fe->sql, "st_transform(");
    }

    n = n->children;
    while (n->type != XML_ELEMENT_NODE)
        n = n->next;

    /* FIXME add schema name ! */ 
    /* display the property name */
    fe->sql = fe_property_name(o, typename, fe, fe->sql, n, true);

    if (transform) {
        buffer_add(fe->sql, ',');
        buffer_add_int(fe->sql, o->request->request.wfs->srs->srid);
        buffer_add(fe->sql, ')');
    }

    n = n->next;

    while (n->type != XML_ELEMENT_NODE)
        n = n->next;

    buffer_add_str(fe->sql, ",");

    /* display the geometry matching the bbox */
    if (strcmp((char *) n->name, "Box") == 0
            || strcmp((char *) n->name, "Envelope") == 0)
        fe->sql = fe_envelope(o, typename, fe, n);

    buffer_add_str(fe->sql, ")");

    return fe->sql;
}


/*
 * Print the SQL request matching spatial function (Equals,Disjoint, etc)
 * Warning : before calling this function,
 * Check if the node name is a spatial operator with fe_is_spatial_op()
 */
buffer *fe_spatial_op(ows * o, buffer * typename, filter_encoding * fe,
                      xmlNodePtr n)
{
    assert(o != NULL);
    assert(typename != NULL);
    assert(fe != NULL);
    assert(n != NULL);

    /* case sensitive comparison because the gml standard specifies
       strictly the name of the operator */
    if (strcmp((char *) n->name, "Equals") == 0
            || strcmp((char *) n->name, "Disjoint") == 0
            || strcmp((char *) n->name, "Touches") == 0
            || strcmp((char *) n->name, "Within") == 0
            || strcmp((char *) n->name, "Overlaps") == 0
            || strcmp((char *) n->name, "Crosses") == 0
            || strcmp((char *) n->name, "Intersects") == 0
            || strcmp((char *) n->name, "Contains") == 0)
        fe->sql = fe_spatial_functions(o, typename, fe, n);
    else if (strcmp((char *) n->name, "DWithin") == 0
             || strcmp((char *) n->name, "Beyond") == 0)
        fe->sql = fe_distance_functions(o, typename, fe, n);
    else if (strcmp((char *) n->name, "BBOX") == 0)
        fe->sql = fe_bbox(o, typename, fe, n);
    else
        fe->error_code = FE_ERROR_FILTER;

    return fe->sql;
}


/*
 * vim: expandtab sw=4 ts=4
 */
