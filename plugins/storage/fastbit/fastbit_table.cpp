/**
 * \file fastbit_table.cpp
 * \author Petr Kramolis <kramolis@cesnet.cz>
 * \brief methods of object wrapers for information elements.
 *
 * Copyright (C) 2011 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

extern "C" {
#include <ipfixcol/verbose.h>
}
#include <vector>
#include "fastbit_table.h"


#define ROW_LINE "Number_of_rows ="

uint64_t get_rows_from_part(const char *part_path){
	uint64_t rows = 0;
	std::string line;
	std::string str_row;
	size_t pos;

	std::ifstream part_file(part_path);
	if(!part_file.is_open()){
		return rows;
	}
	while(getline(part_file, line, '\n')){
		if((pos =line.find(ROW_LINE)) != std::string::npos){
			str_row = line.substr(pos + strlen(ROW_LINE));
			rows = strtoul(str_row.c_str(),NULL,0);
		}
	}
	return rows;
}

template_table::~template_table(){
	for (el_it = elements.begin(); el_it!=elements.end(); ++el_it) {
		(*el_it)->free_buffer();
		delete (*el_it);
	}

	//delete _tablex;
}

int template_table::store(ipfix_data_set * data_set, std::string path){
	uint8_t *data = data_set->records;
	unsigned int record_cnt = 0;
	if (data == NULL){
		return 0;
	}

	//count how many records does data_set contain
	unsigned int data_size = (ntohs(data_set->header.length)-(sizeof(struct ipfix_set_header)));

        //for(ri=0;ri<record_count;ri++){
        unsigned int read_data = 0;
        while(read_data < data_size){
		if((data_size - read_data) < _min_record_size){
			//std::cout << "skip padding ( "<< data_size - read_data <<"b)"<< std::endl;
			break;
		}
		record_cnt++;
		for (el_it = elements.begin(); el_it!=elements.end(); ++el_it) {
			//CHECK DATA SIZE?!?
			(*el_it)->fill(data);
			//_tablex->append((*el_it)->name(), _rows_count, _rows_count+1, (*el_it)->value);
			data += (*el_it)->size();
			read_data += (*el_it)->size();
		}
		_rows_count++;
		if(_rows_count >= _buff_size){
			//_tablex->write((path + _name).c_str(),_name, "Generated by ipfixcol fastbit plugin", &_index);
			_rows_in_window += _rows_count;
			this->dir_check(path + _name);
			//_tablex->clearData();
			for (el_it = elements.begin(); el_it!=elements.end(); ++el_it) {
				(*el_it)->flush(path + _name);
			}
			//std::cout << "BUFFER SIZE -> FLUSH: WRITEN " << _name << " rows: " << _rows_count << std::endl;
			_rows_count = 0;
		}
	}
	return record_cnt;
}

int template_table::parse_template(struct ipfix_template * tmp,struct fastbit_config * config){
	int i;
	uint32_t en = 0; // enterprise number (0 = IANA elements)
	int en_offset = 0;
	template_ie *field;
	element *new_element;
	//Is there anything to parse?
	if(tmp == NULL){
		return 1;
	}
	//we dont want to parse option tables ect. so check it!
	if(tmp->template_type != TM_TEMPLATE){
		return 1; 
	}
	_template_id = tmp->template_id;
	//_record_size = tmp->data_length;

	//Find elements
	for(i=0;i<tmp->field_count + en_offset;i++){
		field = &(tmp->fields[i]);
		if(field->ie.length == 65535){
			_min_record_size += 1;
		} else {
			_min_record_size += field->ie.length;
		}
		
		//Is this an enterprise element?
		en = 0;
		if(field->ie.id & 0x8000){
			i++;
			en_offset++;
			en = tmp->fields[i].enterprise_number;
		}
		//TODO std::cout << "element size:" << field->ie.length << " id:" << (field->ie.id & 0x7FFF) << " en:" << en << std::endl;
		switch(get_type_from_xml(config, en, field->ie.id & 0x7FFF)){
			case UINT:
				new_element = new el_uint(field->ie.length, en, field->ie.id & 0x7FFF, _buff_size);
				//_tablex->addColumn(new_element->name(), new_element->type());
				break;
			case IPv6:
				/* ipv6 is 128b so we have to split it to two 64b rows!
                                 * adding p0 p1 sufixes to row name
                                 */
				//_tablex->addColumn(new_element->name(), new_element->type());
				new_element = new el_ipv6(sizeof(uint64_t), en, field->ie.id & 0x7FFF, 0, _buff_size);
				elements.push_back(new_element);

				new_element = new el_ipv6(sizeof(uint64_t), en, field->ie.id & 0x7FFF, 1, _buff_size);
				//_tablex->addColumn(new_element->name(), new_element->type());
				break;
			case INT: 
				new_element = new el_sint(field->ie.length, en, field->ie.id & 0x7FFF, _buff_size);
				//_tablex->addColumn(new_element->name(), new_element->type());
				break;
			case FLOAT:
				new_element = new el_float(field->ie.length, en, field->ie.id & 0x7FFF, _buff_size);
				//_tablex->addColumn(new_element->name(), new_element->type());
				break;
			case TEXT:
				new_element = new el_text(field->ie.length, en, field->ie.id & 0x7FFF, _buff_size);
				break;
			case BLOB:
			case UNKNOWN:
			default:
				MSG_DEBUG(msg_module,"Received UNKNOWN element (size: %u)",field->ie.length);
				if(field->ie.length < 9){
					new_element = new el_uint(field->ie.length, en, field->ie.id & 0x7FFF, _buff_size);
				} else if(field->ie.length == 65535){ //variable size element
					new_element = new el_var_size(field->ie.length, en, field->ie.id & 0x7FFF, _buff_size);
				} else { //TODO blob ect
					new_element = new el_blob(field->ie.length, en, field->ie.id & 0x7FFF, _buff_size);
				}
				break;
			}
		if(!new_element){
			MSG_ERROR(msg_module,"Something is wrong with template elements!");
			return 1;
		}
		elements.push_back(new_element);
	}
	//_tablex->reserveSpace(RESERVED_SPACE);
	return 0;
}

