from flask import Flask, render_template, session, request, flash, redirect, url_for
import random
import json

app = Flask(__name__)
app.secret_key = "sekret"

WORK_SIZE = 100_000_000

def block_generator():
    for _id, start_seed in enumerate(range(0, 2**64, WORK_SIZE)):
        yield {
            "completed": False,
            "id": _id,
            "start_seed": str(start_seed),
            "end_seed": str(start_seed + WORK_SIZE)
        }    

work_generator = block_generator()

temp_database = {
    "users": {
        "scriptlinestudios@protonmail.com": {
            "username": "ScriptLine",
            "password": "",
            "active_devices": 0,
            "device_ids": [13829, 2198, 21, 123, 321],
        }
    },
    "devices": {

    },
    "work": {
        "incomplete_blocks": [],
        "active_pool": [],
    }
}

@app.route("/")
def home():
    return render_template("index.html", user=session.get("user"))

@app.route("/account")
def account():
    return render_template("account.html", user=session.get("user"))

@app.route("/signup")
def signup():
    return render_template("signup.html")
    
@app.route("/signup_account", methods=["POST"])
def signup_account():
    print(request.form)
    username = request.form.get("display_name")
    email = request.form.get("email")
    password = request.form.get("password")
    password_repeat = request.form.get("password_confirm")

    if password != password_repeat:
        flash("Passwords don't match!")
        return redirect(url_for("signup"))

    temp_database["users"][email] = {
        "username": username, 
        "password": password
    }

    session["user"] = username # this will soon change :)

    return redirect(url_for("home"))

@app.route("/device_sign_out", methods=["POST"])
def device_signout():  
    content = json.loads(str(request.stream.read().decode()))
    device_id = int(content.get("device_id"))
    
    email = temp_database["devices"][device_id]["owner"]
    temp_database["users"].get(email)["active_devices"] -= 1

    # if a device that signs out has work, move it back into incomplete
    if temp_database["devices"][device_id]["has_work"]:
        wid = temp_database["devices"][device_id]["work_id"]
        for work_block in temp_database["work"]["active_pool"]:
            if work_block["id"] == wid:
                temp_database["work"]["active_pool"].remove(work_block)
                temp_database["work"]["incomplete_blocks"].append(work_block)

    del temp_database["devices"][device_id]

    return {}, 200

@app.route("/device_submit_work", methods=["POST"])
def device_submit_work():
    content = json.loads(str(request.stream.read().decode()))
    seeds = content.get("results")
    device_id = content.get("device_id")

    if temp_database["devices"][device_id]["has_work"]:
        wid = temp_database["devices"][device_id]["work_id"]
        for work_block in temp_database["work"]["active_pool"]:
            if work_block["id"] == wid:
                print(f"WORK BLOCK: {wid} IS NOW COMPLETE!")
                temp_database["work"]["active_pool"].remove(work_block)

    temp_database["devices"][device_id]["has_work"] = False

    return {}, 200
    # device_id = int(content.get("device_id"))

@app.route("/device_get_work", methods=["POST"])
def device_get_work():
    content = json.loads(str(request.stream.read().decode()))
    print(content)
    device_id = int(content.get("device_id"))

    if temp_database["devices"][device_id].get("has_work"):
        return {
            "message": "[SERVER]: Device already has work!" 
        }, 401

    if len(temp_database["work"]["incomplete_blocks"]) == 0:
        # generate more work
        for i in range(5):
            temp_database["work"]["incomplete_blocks"].append(next(work_generator))

    work = temp_database["work"]["incomplete_blocks"][0]
    temp_database["work"]["incomplete_blocks"].remove(work)
    temp_database["work"]["active_pool"].append(work)

    temp_database["devices"][device_id]["has_work"] = True
    temp_database["devices"][device_id]["work_id"] = work.get("id")

    return work, 200

@app.route("/device_signin", methods=["POST"])
def device_signin():  
    content = json.loads(str(request.stream.read().decode()))
    email = content.get("email")
    password = content.get("password")

    if email not in temp_database["users"].keys():
        return {
            "message": f"[SERVER]: Account not found! Please make an account on https://seedhunt.org",
            "device_id": -1,
        }, 401

    if password == temp_database["users"].get(email).get("password"):
        active_devices = temp_database["users"].get(email).get("active_devices")
        device_id = temp_database["users"].get(email).get("device_ids")[active_devices]
        temp_database["users"].get(email)["active_devices"] += 1
        print(device_id)

        temp_database["devices"][device_id] = {
            "owner": email,
            "has_work": False,
            "work_id": -1,
        }

        return {
            "message": f"[SERVER]: Welcome {temp_database['users'][email].get('username')}!",
            "device_id": device_id
        }
    
    return {
        "message": f"[SERVER]: Incorrect password!",
        "device_id": -1,
    }, 401

if __name__ == "__main__":
    app.run(debug=True)
