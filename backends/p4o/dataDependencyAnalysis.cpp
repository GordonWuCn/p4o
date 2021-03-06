#include "dataDependencyAnalysis.h"
namespace P4O{
bool DataDependencyAnalysis::preorder(const IR::P4Program* p){
    auto info = new DependencyInfo;
    dependency_map.emplace(p, info);
    const IR::Node *ingress, *egress;
    for(auto n : p->objects){
        if(auto control = n->to<IR::P4Control>()){
            if(control->name == v1arch->ingress->name){
                ingress = control;
            }
            else if(control->name == v1arch->egress->name){
                egress = control;
            }
        }
    }
    info->ordered_control.push_back(ingress);
    info->ordered_control.push_back(egress);
    sanity(ingress);
    visit(ingress);
    sanity(egress);
    visit(egress);
    return false;
}

bool DataDependencyAnalysis::preorder(const IR::P4Parser*){
    return false;
}

bool DataDependencyAnalysis::preorder(const IR::P4Control *c){
    auto info = new DependencyInfo;
    dependency_map.emplace(c, info);
    info->ordered_control.push_back(c->body);
    sanity(c->body);
    visit(c->body);
    return false;
}
bool DataDependencyAnalysis::preorder(const IR::BlockStatement *b){
    auto info = new DependencyInfo;
    dependency_map.emplace(b, info);
    for(auto stmt: b->components){
        sanity(stmt);
        info->ordered_control.push_back(stmt);
        visit(stmt);
    }
    return false;
}
bool DataDependencyAnalysis::preorder(const IR::P4Table *t){
    auto info = new DependencyInfo;
    dependency_map.emplace(t, info);
    for(auto action: t->getActionList()->actionList){
        if(auto mce = action->expression->to<IR::MethodCallExpression>()){
            auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
            if(auto action_call = mi->to<P4::ActionCall>()){
                info->unordered_control.push_back(action_call->action);
                sanity(action_call->action);
                visit(action_call->action);
            }
            else{
                BUG("Action Call");
            }
        }
        else{
            BUG("action is not a MethodCallExpression");
        }
    }
    if(not t->getKey()){
        return false;
    }
    for(auto key: t->getKey()->keyElements){
        info->read_list.insert(key->expression);
    }
    return false;
}
bool DataDependencyAnalysis::preorder(const IR::P4Action *a){
    auto info = new DependencyInfo;
    dependency_map.emplace(a, info);
    info->ordered_control.push_back(a->body);
    sanity(a->body);
    visit(a->body);
    return false;
}
bool DataDependencyAnalysis::preorder(const IR::IfStatement *i){
    auto info = new DependencyInfo;
    dependency_map.emplace(i, info);
    if(i->ifTrue) {
        info->unordered_control.push_back(i->ifTrue);
        sanity(i->ifTrue);
        visit(i->ifTrue);
    }
    if(i->ifFalse) {
        info->unordered_control.push_back(i->ifFalse);
        sanity(i->ifFalse);
        visit(i->ifFalse);
    }
    auto expr_bd = new ExpressionBreakdown(refMap, typeMap);
    i->condition->apply(*expr_bd);
    info->read_list.insert(
        expr_bd->read_list.begin(), expr_bd->read_list.end());
    info->write_list.insert(
        expr_bd->write_list.begin(), expr_bd->write_list.end());
    return false;
}

bool DataDependencyAnalysis::preorder(const IR::MethodCallStatement *mcs){
    auto info = new DependencyInfo;
    dependency_map.emplace(mcs, info);
    auto mi = P4::MethodInstance::resolve(mcs, refMap, typeMap);
    if (auto apply = mi->to<P4::ApplyMethod>()){
        if (auto table = apply->object->to<IR::P4Table>()){
            info->ordered_control.push_back(table);
            sanity(table);
            visit(table);
        }
        else{
            std::cout << mcs << std::endl;
            BUG("should not happen");
        }
    }
    else {
        auto expr_bd = new ExpressionBreakdown(refMap, typeMap);
        mcs->apply(*expr_bd);
        info->read_list.insert(
            expr_bd->read_list.begin(), expr_bd->read_list.end());
        info->write_list.insert(
            expr_bd->write_list.begin(), expr_bd->write_list.end());
    }
    
    return false;
}

bool DataDependencyAnalysis::preorder(const IR::AssignmentStatement *a){
    auto info = new DependencyInfo;
    dependency_map.emplace(a, info);
    info->write_list.insert(a->left);
    auto expr_bd = new ExpressionBreakdown(refMap, typeMap);
    a->right->apply(*expr_bd);
    info->read_list.insert(
        expr_bd->read_list.begin(), expr_bd->read_list.end());
    info->write_list.insert(
        expr_bd->write_list.begin(), expr_bd->write_list.end());
    return false;
}

bool ExpressionBreakdown::preorder(const IR::Operation_Binary *bin){
    sanity(bin->left);
    visit(bin->left);
    sanity(bin->right);
    visit(bin->right);
    return false;
}
bool ExpressionBreakdown::preorder(const IR::MethodCallExpression *mce){
    auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
    if(auto em = mi->to<P4::ExternMethod>()){
        if(em->originalExternType->getName() == "register"){
            if(em->method->getName() == "read"){
                auto result = (*em->expr->arguments)[0]->expression;
                auto index = (*em->expr->arguments)[1]->expression;
                simple_expr_sanity(result);
                simple_expr_sanity(index);
                read_list.insert(index);
                write_list.insert(result);
            }
            else if(em->method->getName() == "write"){
                auto index = (*em->expr->arguments)[0]->expression;
                auto value = (*em->expr->arguments)[1]->expression;
                simple_expr_sanity(index);
                read_list.insert(index);
                simple_expr_sanity(value);
                write_list.insert(value);
            }
            else{
                BUG("not implemented");
            }
        }
        else{
            BUG("not implemented");
        }
    }
    else if(auto builtin = mi->to<P4::BuiltInMethod>()){
        if(builtin->name == "setValid"){
            write_list.insert(builtin->appliedTo);
        }
        else if(builtin->name == "setInvalid"){
            write_list.insert(builtin->appliedTo);
        }
        else if(builtin->name == "isValid") {
            read_list.insert(builtin->appliedTo);
        }
        else if(builtin->name == "isInvalid") {
            read_list.insert(builtin->appliedTo);
        }
        else{
            std::cerr << mce << std::endl;
            BUG("not implemented");
        }
    }
    else if(auto ef = mi->to<P4::ExternFunction>()){
        if(ef->method->getName()  == "hash"){
            auto out = (*ef->expr->arguments)[0]->expression;
            simple_expr_sanity(out);
            write_list.insert(out);
            auto in = (*ef->expr->arguments)[3]->expression;
            if(auto list = in->to<IR::ListExpression>()){
                for(auto element : list->components){
                    simple_expr_sanity(element);
                    read_list.insert(element);
                }
            }
            else{
                simple_expr_sanity(in);
                read_list.insert(in);
            }
        }
        else if(ef->method->getName() == "clone3"){

        }
        else if(ef->method->getName() == "route_by_ip"){

        }
        else if(ef->method->getName() == "route_to_program"){
            
        }
        else if(ef->method->getName() == "harmony_e2e_clone_to_program"){
            
        }
        else if(ef->method->getName() == "harmony_e2e_clone_by_ip"){
            
        }
        else if(ef->method->getName() == "harmony_is_normal"){
            
        }
        else{
            std::cerr << mce << std::endl;
            BUG("not implemented");
        }
    }
    else {
        std::cerr << mce << std::endl;
        BUG("not implemented");
    }
    return false;
}
bool ExpressionBreakdown::preorder(const IR::Constant *){
    return false;
}
bool ExpressionBreakdown::preorder(const IR::Member *m){
    read_list.insert(m);
    return false;
}
bool ExpressionBreakdown::preorder(const IR::PathExpression *p){
    read_list.insert(p);
    return false;
}

bool ExpressionBreakdown::preorder(const IR::LNot *lnot){
    sanity(lnot->expr);
    visit(lnot->expr);
    return false;
}
bool ExpressionBreakdown::preorder(const IR::Cast *cast){
    sanity(cast->expr);
    visit(cast->expr);
    return false;
}
bool ExpressionBreakdown::preorder(const IR::Slice *slice){
    std::cerr << slice->e0 << std::endl;
    assert(0);
    return false;
}

bool LiftDependencyToTopLevel::preorder(const IR::P4Program*){
    std::map<const IR::Node*, int> degree_map;
    std::map<const IR::Node*, std::set<const IR::Node*>*> reverse_map;
    for(auto it: *dependency_map){
        degree_map.emplace(it.first, 0);
        reverse_map.emplace(it.first, new std::set<const IR::Node*>);
    }
    for(auto it: *dependency_map){
        auto res = degree_map.find(it.first);
        if(res != degree_map.end()){
            res->second = it.second->unordered_control.size() + 
                it.second->ordered_control.size();
        }
        else {
            BUG("degree map error");
        }
        for(auto ordered: it.second->ordered_control){
            auto res2 = reverse_map.find(ordered);
            if(res2 != reverse_map.end()){
                res2->second->insert(it.first);
            }
            else {
                BUG("reverse map error");
            }
        }
        for(auto unordered: it.second->unordered_control){
            auto res2 = reverse_map.find(unordered);
            if(res2 != reverse_map.end()){
                res2->second->insert(it.first);
            }
            else {
                BUG("reverse map error");
            }
        }
    }
    std::vector<const IR::Node*> to_delete;
    while(degree_map.size() > 0){
        for(auto it: degree_map){
            if(degree_map.size() == 1){
                auto dup = it.first;
                *top_level_block = dup;
            }
            if(it.second == 0){
                to_delete.push_back(it.first);            
            }
        }
        for(auto it: to_delete){
            degree_map.erase(it);
            auto res = reverse_map.find(it);
            auto child_info = dependency_map->find(it);
            if(child_info == dependency_map->end()){
                BUG("impossible logic");
            }
            if(res != reverse_map.end()){
                for(auto parent: *res->second){
                    auto res2 = degree_map.find(parent);
                    if(res2 != degree_map.end()){
                        res2->second--;
                    }
                    else{
                        BUG("reverse map consturction failed");
                    }
                    auto parent_info = dependency_map->find(parent);
                    if(parent_info != dependency_map->end()){
                        parent_info->second->write_list.insert(
                            child_info->second->write_list.begin(),
                            child_info->second->write_list.end()
                        );
                        parent_info->second->read_list.insert(
                            child_info->second->read_list.begin(),
                            child_info->second->read_list.end()
                        );
                    }
                }
            }
            else{
                std::cerr << it << std::endl;
                BUG("reverse map construction failed");
            }
        }
        to_delete.clear();
    }
    return false;
}

int GenerateDependencyGraph::
compare(const IR::Node *table1, const IR::Node *table2, DependencyInfo *info){
    int state = init;
    for(auto it: info->ordered_control){
        if(it->equiv(*table1)){
            if(state == init){
               state = find_table1;
            }
            else if(state == find_table2){
                return after;
            }
            else{
                BUG("state is wrong");
            }
        }
        else if(it->equiv(*table2)){
            if(state == init){
                state = find_table2;
            }
            else if(state == find_table1){
                return before;
            }
            else BUG("state is wrong");
        }
        else{
            if(not it->is<IR::P4Table>()){
                auto info = orig_dependency_map->find(it);
                if(info != orig_dependency_map->end()){
                    auto res = compare(table1, table2, info->second);
                    if(res == before || res == after || res == dont_care) 
                        return res;
                    else if(res == find_table1){
                        if(state == init){
                            state = res;
                        }
                        else if(state == find_table2){
                            return after;
                        }
                        else{
                            BUG("state is wrong");
                        }
                    }
                    else if(res == find_table2){
                        if(state == init){
                            state = res;
                        }
                        else if(state == find_table1){
                            return before;
                        }
                        else{
                            BUG("state is wrong");
                        }
                    }
                    else if(not (res == init)){
                        BUG("impossible state");
                    }
                }
                else{
                    BUG("orig_dependency_map error");
                }
            }
        }
    }
    bool found_in_unordered = false;
    for(auto it: info->unordered_control){
        if(it->equiv(*table1)){
            if(state == init){
                found_in_unordered = true;
                state = find_table1;
            }
            else if(state == find_table2){
                if(not found_in_unordered) BUG("cross ordered and unordered");
                return dont_care;
            }
            else{
                BUG("state error");
            }
        }
        else if(it->equiv(*table2)){
            if(state == init){
                found_in_unordered = true;
                state = find_table2;
            }
            else if(state == find_table1){
                if(not found_in_unordered) BUG("cross ordered and unordered");
                return dont_care;
            }
            else{
                BUG("state error");
            }
        }
        else{
            if(not it->is<IR::P4Table>()){
                auto info = orig_dependency_map->find(it);
                if(info != orig_dependency_map->end()){
                    auto res = compare(table1, table2, info->second);
                    if(res == before || res == after || res == dont_care) 
                        return res;
                    else if(res == find_table1){
                        if(state == init){
                            found_in_unordered = true;
                            state = res;
                        }
                        else if(state == find_table2){
                            if(not found_in_unordered) 
                                BUG("cross ordered and unordered");
                            return dont_care;
                        }
                        else{
                            BUG("state is wrong");
                        }
                    }
                    else if(res == find_table2){
                        if(state == init){
                            found_in_unordered = true;
                            state = res;
                        }
                        else if(state == find_table1){
                            if(not found_in_unordered)
                                BUG("cross ordered and unordered");
                            return dont_care;
                        }
                        else{
                            BUG("state is wrong");
                        }
                    }
                    else if(not (res == init)) {
                        BUG("impossible state");
                    }
                }
                else{
                    BUG("orig_dependency_map error");
                }
            }
        }
    }
    return state;
}

int GenerateDependencyGraph::
has_dependency(const IR::Node *table1, const IR::Node *table2, Util::JsonArray * variable_shared){
    int has_dep = no_dependency;
    auto it1 = dependency_map->find(table1);
    auto it2 = dependency_map->find(table2);
    DependencyInfo *info1, *info2;
    if(it1 != dependency_map->end()){
        info1 = it1->second;
    }
    else BUG("table1 not found in dependency_map");
    if(it2 != dependency_map->end()){
        info2 = it2->second;
    }
    else BUG("table1 not found in dependency_map");
    for(auto it1: info1->read_list){
        for(auto it2: info2->write_list){
            if(it1->equiv(*it2)) {
                auto variable = new Util::JsonObject();
                variable->emplace("read_after_write", it1->toString());
                variable_shared->append(variable);
                has_dep = dependency;
            }
        }
    }
    for(auto it1: info1->write_list){
        for(auto it2: info2->write_list){
            if(it1->equiv(*it2)) {
                auto variable = new Util::JsonObject();
                variable->emplace("write_after_write", it1->toString());
                variable_shared->append(variable);
                has_dep = dependency;
            }
        }
    }
    for(auto it1: info1->write_list){
        for(auto it2: info2->read_list){
            if(it1->equiv(*it2)) {
                auto variable = new Util::JsonObject();
                variable->emplace("write_after_read", it1->toString());
                variable_shared->append(variable);
                has_dep = dependency;
            }
        }
    }
    return has_dep;
}


bool GenerateDependencyGraph::preorder(const IR::P4Program*){
    // for(auto it: *dependency_map){
    //     if(it.first->is<IR::IfStatement>()){
    //         std::cerr << it.first << std::endl;
    //         std::cerr << *it.second << std::endl;
    //     }
    // }
    DependencyInfo *top_info;
    auto top_info_res = orig_dependency_map->find(*top_level_block);
    if(top_info_res != orig_dependency_map->end()){
        top_info = top_info_res->second;
    }
    else{
        BUG("top block not found");
    }
    auto dep_array = new Util::JsonArray();
    for(auto it1: *dependency_map){
        if(auto if1 = it1.first->to<IR::IfStatement>()){
            auto table1 = if1->ifTrue;
            auto table1_name = table1->to<IR::MethodCallStatement>()->methodCall->method->to<IR::Member>()->expr->to<IR::PathExpression>()->path->name.name;
            bool placed = false;
            for(auto it2: *dependency_map){
                if(auto if2 = it2.first->to<IR::IfStatement>()){
                    if(if1->equiv(*if2)) continue;
                    auto table2 = if2->ifTrue;
                    auto cmp_res = compare(table1, table2, top_info);
                    auto variable_shared = new Util::JsonArray();
                    auto has_dep = has_dependency(if1, if2, variable_shared);
                    auto table2_name = table2->to<IR::MethodCallStatement>()->methodCall->method->to<IR::Member>()->expr->to<IR::PathExpression>()->path->name.name;
                    if(cmp_res == init or cmp_res == find_table1 or cmp_res == find_table2){
                        std::cerr << table1 << std::endl << std::endl;
                        std::cerr << table2 << std::endl << std::endl;
                        BUG("compare error");
                    }
                    else if(cmp_res == after){
                        if(
                            has_dep == dependency
                        ){
                            auto dep_obj = new Util::JsonObject();
                            dep_obj->emplace("variables_shared", variable_shared);
                            dep_obj->emplace("leading", table2_name);
                            dep_obj->emplace("following", table1_name);
                            dep_array->append(dep_obj);
                            // std::cout << table1_name << " -> " << table2_name 
                            //     << ";" << std::endl;
                            // if(has_dep == write_after_write){
                            //     std::cout << "write_after_write" << std::endl;
                            // }
                            // else if(has_dep == write_after_read){
                            //     std::cout << "write_after_read" << std::endl;
                            // }
                            // else if(has_dep == read_after_write){
                            //     std::cout << "read_after_write" << std::endl;
                            // }
                            placed = true;
                        }
                    }
                    else if(not (cmp_res == dont_care or cmp_res == before)) 
                        BUG("compare function state is wrong");
                }
            }
            if(not placed){
                // std::cout << table1_name << ";" << std::endl;
            }
        }
    }
    table_info->emplace("dependency", dep_array);
    return false;
}


bool CollectRuntimeName::preorder(const IR::P4Table* table){
    auto annt = table->annotations->annotations[0];
    if(annt->name == annt->nameAnnotation){
        auto runtime_name = std::string(annt->expr[0]->to<IR::StringLiteral>()->value.c_str());
        std::stringstream ss(runtime_name);
        std::string token;
        std::vector<std::string> cont;
        while (std::getline(ss, token, '.')) {
            cont.push_back(token);
        }
        auto real_table_name = cont.back();
        table_translation->emplace(cstring(real_table_name), table->name);
    }
    return false;
}


bool CollectRuntimeName::preorder(const IR::P4Action* action){
    auto annt = action->annotations->annotations[0];
    if(annt->name == annt->nameAnnotation){
        auto runtime_name = std::string(annt->expr[0]->to<IR::StringLiteral>()->value.c_str());
        std::stringstream ss(runtime_name);
        std::string token;
        std::vector<std::string> cont;
        while (std::getline(ss, token, '.')) {
            cont.push_back(token);
        }
        auto real_action_name = cont.back();
        action_translation->emplace(cstring(real_action_name), action->name);
    }
    return false;
}


bool CollectTableInfo::preorder(const IR::P4Control* c){
    if(c->name != v1arch->ingress->name and c->name != v1arch->egress->name){
        return false;
    }
    return true;
}
bool CollectTableInfo::preorder(const IR::P4Table* t) {
    auto size = t->properties->getProperty(t->properties->sizePropertyName);
    auto control = findContext<IR::P4Control>();
    cstring control_name;
    if(control->name.name == v1arch->ingress->name.name){
        control_name = "ingress";
    }
    else{
        control_name = "egress";
    }

    if(size) {
        if(auto value = size->value->to<IR::ExpressionValue>()){
            if(auto s = value->expression->to<IR::Constant>()){
                auto obj = new Util::JsonObject();
                obj->emplace("id", t->name.name);
                obj->emplace("size", s->value);
                obj->emplace("control", control_name);
                info->append(obj);
            }
            else{
                std::cerr << value->expression->node_type_name() << std::endl;
                BUG("unexpected expression type");
            }
        }
        else{
            std::cerr << size->value->node_type_name() << std::endl;
            BUG("unexpected property value type");
        }
    }
    else{
        auto obj = new Util::JsonObject();
        obj->emplace("id", t->name.name);
        obj->emplace("size", 0);
        obj->emplace("control", control_name);
        info->append(obj);
    }
    return false;
}

void CollectTableInfo::postorder(const IR::P4Program *){
    table_info->serialize(std::cout);
}

bool CollectHeaderAccessInfo::preorder(const IR::P4Parser*){
    return true;
}
bool CollectHeaderAccessInfo::preorder(const IR::SelectExpression* select){
    if(findContext<IR::P4Parser>()){
        header_accessed_in_parser.append(select->select->components);
    }
    else{
        BUG("select expression outside parser");
    }
    return false;
}

bool CollectHeaderAccessInfo::preorder(const IR::Member* member){
    if(findContext<IR::P4Control>()){
        auto type = typeMap->getType(member->expr);
        if(type->is<IR::Type_Header>()){
            if(findContext<IR::MethodCallStatement>()){
                if(member->member == "setValid"){
                    header_set_valid.push_back(member->expr);
                }
                else if(member->member == "setInvalid"){
                    header_set_invalid.push_back(member->expr);
                }
                else if(member->member == "isValid"){
                    header_is_valid.push_back(member->expr);
                }
                else if(member->member == "isInvalid"){
                    header_is_invalid.push_back(member->expr);
                }
                // else {
                //     std::cerr << member << std::endl;
                //     BUG("not implemented");
                // }
            }
        }
    }
    return true;
} 

bool ExtractHeaderAccess::preorder(const IR::P4Program* ){
    DependencyInfo *top_info;
    auto top_info_res = dependency_map->find(*top_block);
    if(top_info_res != dependency_map->end()){
        top_info = top_info_res->second;
    }
    else{
        BUG("top block not found");
    }
    for(auto header : header_access->header_accessed_in_parser){
        for(auto var : top_info->write_list){
            if(header->equiv(*var)){
                if(auto member = header->to<IR::Member>()){
                    auto field = member->member;
                    if(auto header_member = member->expr->to<IR::Member>()){
                        auto header_name = header_member->member;
                        auto obj = new Util::JsonObject;
                        obj->emplace("header_name", header_name.name);
                        obj->emplace("field_name", field.name);
                        auto bits = header->type->to<IR::Type_Bits>();
                        obj->emplace("bits", bits->size);
                        header_field_accessed->append(obj);
                        // std::cerr << header_name << std::endl;
                    }
                    else BUG("header not in xx.xx.xx format");
                }
                else{
                    BUG("header not in xx.xx.xx format");
                }
            }
        }
    }
    for(auto header: header_access->header_set_valid){
        for(auto var: top_info->write_list){
            if(auto member = var->to<IR::Member>()){
                if(header->equiv(*member->expr)){
                    auto field = member->member;
                    if(auto header_member = member->expr->to<IR::Member>()){
                        auto header_name = header_member->member;
                        auto obj = new Util::JsonObject;
                        obj->emplace("header_name", header_name.name);
                        obj->emplace("field_name", field.name);
                        auto bits = member->type->to<IR::Type_Bits>();
                        obj->emplace("bits", bits->size);
                        header_field_accessed->append(obj);
                    }
                    else BUG("header not in xx.xx.xx format");
                }
            }
        }
        if(auto member = header->to<IR::Member>()){
            header_set_valid->append(member->member.name);
        }
        else BUG("header not in xx.xx format");
    }

    for(auto header: header_access->header_set_invalid){
        if(auto member = header->to<IR::Member>()){
            header_set_invalid->append(member->member.name);
        }
        else BUG("header not in xx.xx format");
    }
    for(auto header: header_access->header_is_valid){
        for(auto header2 : header_access->header_set_valid){
            if(header->equiv(*header2)){
                header_is_valid->append(header->to<IR::Member>()->member.name);
            }
        }
        for(auto header2 : header_access->header_set_invalid){
            if(header->equiv(*header2)){
                header_is_valid->append(header->to<IR::Member>()->member.name);
            }
        }
    }
    for(auto header: header_access->header_is_invalid){
        for(auto header2 : header_access->header_set_valid){
            if(header->equiv(*header2)){
                header_is_invalid->append(header->to<IR::Member>()->member.name);
            }
        }
        for(auto header2 : header_access->header_set_invalid){
            if(header->equiv(*header2)){
                header_is_invalid->append(header->to<IR::Member>()->member.name);
            }
        }
    }

    return false;   
}

bool CollectTempVariableAccess::preorder(const IR::P4Program*){
    auto metadata = v1arch->ingress->type->applyParams->parameters[1]->type->to<IR::Type_Name>()->path->name.name;
    auto header = v1arch->ingress->type->applyParams->parameters[0]->type->to<IR::Type_Name>()->path->name.name;
    for(auto it: *new_dependency_map){
        if(auto if_stmt = it.first->to<IR::IfStatement>()){
            auto table_name = if_stmt->ifTrue->to<IR::MethodCallStatement>()->methodCall->method->to<IR::Member>()->expr->to<IR::PathExpression>()->path->name.name;
            auto obj = new Util::JsonObject;
            auto read_list = new Util::JsonArray;
            obj->emplace("read_list", read_list);
            for(auto node: it.second->read_list){
                if(auto member = node->to<IR::Member>()){
                    if(auto meta_name = member->expr->to<IR::PathExpression>()){
                        auto decl = refMap->getDeclaration(meta_name->path);
                        if(auto param = decl->to<IR::Parameter>()){
                            if(param->type->to<IR::Type_Name>()->path->name.name == metadata){
                                auto metadata_obj = new Util::JsonObject;
                                metadata_obj->emplace("metadata_name", member->member.name);
                                int size = member->type->to<IR::Type_Bits>()->size;
                                metadata_obj->emplace("metadata_size", size);
                                read_list->append(metadata_obj);
                            }
                            else if(param->type->to<IR::Type_Name>()->path->name.name == header){}
                            else{
                                std::cerr << param << std::endl;
                                BUG("Not implemented");
                            }
                        }
                        else{
                            std::cerr << decl << std::endl;
                            BUG("not implemented");
                        }
                    }
                }
            }
            auto write_list = new Util::JsonArray;
            obj->emplace("write_list", write_list);
            for(auto node: it.second->write_list){
                if(auto member = node->to<IR::Member>()){
                    if(auto meta_name = member->expr->to<IR::PathExpression>()){
                        auto decl = refMap->getDeclaration(meta_name->path);
                        if(auto param = decl->to<IR::Parameter>()){
                            if(param->type->to<IR::Type_Name>()->path->name.name == metadata){
                                auto metadata_obj = new Util::JsonObject;
                                metadata_obj->emplace("metadata_name", member->member.name);
                                int size = member->type->to<IR::Type_Bits>()->size;
                                metadata_obj->emplace("metadata_size", size);
                                write_list->append(metadata_obj);
                            }
                            else if(param->type->to<IR::Type_Name>()->path->name.name == header){}
                            else{
                                std::cerr << param << std::endl;
                                BUG("Not implemented");
                            }
                        }
                        else{
                            std::cerr << decl << std::endl;
                            BUG("not implemented");
                        }
                    }
                }
            }
            temp_variable_access->emplace(table_name, obj);
        }
    }

    return false;
}



bool CollectEgressCloneReservationInfo::preorder(const IR::MethodCallStatement*mcs){
    if(auto path = mcs->methodCall->method->to<IR::PathExpression>()){
        if(path->path->name.name == "harmony_e2e_clone_by_ip"){
            if(collected){
                BUG("harmony_e2e_clone_by_ip currently only support one harmony_e2e_clone_by_ip call");
            }
            auto args = mcs->methodCall->arguments;
            auto copy_list = (*args)[1];
            if(auto list_expression = copy_list->expression->to<IR::ListExpression>()){
                for(auto it: list_expression->components){
                    if(auto member = it->to<IR::Member>()){
                        auto tuple = new Util::JsonObject;
                        tuple->emplace("name", member->member.name);
                        tuple->emplace("size", member->type->to<IR::Type_Bits>()->size);
                        egress_clone_reserved->append(tuple);
                    }
                    else{
                        std::cerr << it << std::endl;
                        BUG("It is not a member");
                    }
                }
                collected = true;
            }
            else{
                std::cerr << copy_list->expression->node_type_name() << std::endl;
                BUG("harmony_e2e_clone_by_ip's first arg is not a list expression");
            }
        }
    }

    return false;
}

void CollectEgressCloneReservationInfo::postorder(const IR::P4Program*){
    auto has_egress_clone = new Util::JsonValue(collected);
    table_info->emplace("has_egress_clone", has_egress_clone);
}

} //namespace P4O