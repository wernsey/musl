BEGIN { 
	if(!title) title = "Documentation"
	print "<html><head><title>" title "</title>";
	print "<style><!--";
	print "tt,strong {color:#575c91}";
	print "pre {color:#575c91;background:#d4d4ff;border:1px solid #575c91;border-radius:5px;padding:15px;margin-left:30px;margin-right:30px;}";
	print "h2  {color:#5252ef;background:#9191c1;border: 1px solid #575c91;border-radius:5px;padding:20px;text-align:center;}";
	print "h3  {color:#575c91;background:#b8b8e0;border-top:1px solid #575c91;border-radius:5px;padding:10px;font-family:monospace;}";
	print "body {font-family:Arial, Verdana, Helvetica, sans-serif;margin-left:20px;margin-right:20px;}";
	print "--></style>";
	print "</head><body>";
}

/\/\*/ { comment = 1; }

/\*!/ { if(!comment) next; s = substr($0, index($0, "*!") + 2); print "<h2><tt>" filter(s) "</tt></h2>"; next;}
/\*@/ { if(!comment) next; s = substr($0, index($0, "*@") + 2); print "<h3><tt>" filter(s) "</tt></h3>"; next;}
/\*#[ \t]*$/ { if(!comment) next; if(!pre) print "<br>"; next;}
/\*#/ { if(!comment) next; s = substr($0, index($0, "*#") + 2); print filter(s);}
/\*&/ { if(!comment) next; s = substr($0, index($0, "*&") + 2); print "<tt>" filter(s) "</tt><br>"; next;}
/\*x/ { if(!comment) next; s = substr($0, index($0, "*x") + 2); print "<p><strong>Example:</strong><tt>" filter(s) "</tt></p>"; next;}
/\*N/ { if(!comment) next; s = substr($0, index($0, "*N") + 2); print "<p><strong>Note:</strong><em>" filter(s) "</em></p>"; next;}
/\*\[/ { if(!comment) next; pre=1; print "<pre>"; next;}
/\*]/ { if(!comment) next; pre=0; print "</pre>"; next;}
/\*\{/ { if(!comment) next; print "<ul>"; next;}
/\*\*/ { if(!comment) next; s = substr($0, index($0, "**") + 2); print "<li>" filter(s); next;}
/\*}/ { if(!comment) next; print "</ul>"; next;}
/\*-/ { if(!comment) next; print "<hr size=2>"; next;}
/\*=/ { if(!comment) next; print "<hr size=5>"; next;}

/\*\// { comment = 0; }

END { print "</body></html>" }

function filter(ss,        j, k1, k2, k3)
{
	gsub(/&/, "\\&amp;", ss); 
	gsub(/</, "\\&lt;", ss); 
	gsub(/>/, "\\&gt;", ss);
	gsub(/\\n[ \t]*$/, "<br>", ss);
	gsub(/{{/, "<tt>", ss); 
	gsub(/}}/, "</tt>", ss);
	gsub(/{\*/, "<strong>", ss); 
	gsub(/\*}/, "</strong>", ss);
	gsub(/{\//, "<em>", ss); 
	gsub(/\/}/, "</em>", ss);
	gsub(/{_/, "<u>", ss); 
	gsub(/_}/, "</u>", ss);	
	
	# Hyperlinks (excuse my primitive regex)
	gsub(/http:\/\/[a-zA-Z0-9._\/\-%~]+/, "<a href=\"&\">&</a>", ss);
	
	# Use a ##word to specify an anchor, eg. ##foo gets translated to <a name="foo">foo</a>
	while(j = match(ss, /##[A-Za-z0-9_]+/))
	{
		k1 = substr(ss, 1, j - 1);
		k2 = substr(ss, j + 2, RLENGTH-2);
		k3 = substr(ss, j + RLENGTH);
		ss = k1 "<a name=\"" k2 "\">" k2 "</a>" k3
	}
	
	# Use a ~~word to specify an anchor, eg. ~~foo gets translated to <a href="#foo">foo</a>
	while(j = match(ss, /~~[A-Za-z0-9_]+/))
	{
		k1 = substr(ss, 1, j - 1);
		k2 = substr(ss, j + 2, RLENGTH-2);
		k3 = substr(ss, j + RLENGTH);
		ss = k1 "<a href=\"#" k2 "\">" k2 "</a>" k3
	}
	
	gsub(/\*\//, "", ss);
	return ss;
}
